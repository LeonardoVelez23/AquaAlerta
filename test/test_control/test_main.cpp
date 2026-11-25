// Pruebas de la lógica de control en la PC (sin ESP32): pio test -e native
// Usan los tiempos de config.h (MODO_DEMO por defecto).
#include <unity.h>

#include "control.h"

void setUp() {}
void tearDown() {}

// ---------- Conversiones y dosis ----------
void test_pac_tabla() {
  TEST_ASSERT_EQUAL_FLOAT(0, pacPpm(5));      // agua clara
  TEST_ASSERT_EQUAL_FLOAT(20, pacPpm(10));
  TEST_ASSERT_EQUAL_FLOAT(25, pacPpm(15));    // interpolado
  TEST_ASSERT_EQUAL_FLOAT(200, pacPpm(500));  // tope
  TEST_ASSERT_TRUE(pacFueraDeTabla(500));
  // C1·V1 = C2·V2: 200 ppm en 0,5 L con solución al 1 % → 10 ml
  TEST_ASSERT_FLOAT_WITHIN(0.01, 10, mlPac(200, 0.5f));
}

void test_cloro_por_ph() {
  TEST_ASSERT_EQUAL_FLOAT(1, gotasPorLitro(7.0f));
  TEST_ASSERT_EQUAL_FLOAT(1, gotasPorLitro(6.0f));  // ácido: clora pero avisa
  TEST_ASSERT_EQUAL_FLOAT(-1, gotasPorLitro(8.5f)); // alto: no clora
  TEST_ASSERT_EQUAL(3, rangoPh(8.5f));
}

// ---------- Recipientes ----------
void test_tanque_frasco() {
  Tanque frasco = {32, 6, 8};  // frasco de 8 L: 26 cm útiles
  TEST_ASSERT_FLOAT_WITHIN(0.01, 50, frasco.nivel(19));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 4, frasco.litrosDe(50));
  TEST_ASSERT_EQUAL_FLOAT(0, frasco.nivel(40));    // bajo el fondo: 0 %
  TEST_ASSERT_EQUAL_FLOAT(100, frasco.nivel(3));   // sobre el máximo: 100 %
  // 1 % de 26 cm son 2,6 mm (menos que el ruido): el umbral sube a 0,5 cm ≈ 1,9 %
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5f / 26 * 100, frasco.umbral(1));
  TEST_ASSERT_EQUAL_FLOAT(5, frasco.umbral(5));    // si ya es mayor, se respeta
}

void test_tanque_validacion() {
  TEST_ASSERT_TRUE(Tanque::valido(32, 6, 8));
  TEST_ASSERT_FALSE(Tanque::valido(6, 32, 8));     // vacío y lleno al revés
  TEST_ASSERT_FALSE(Tanque::valido(8, 6, 8));      // menos de 3 cm útiles
  TEST_ASSERT_FALSE(Tanque::valido(32, 1, 8));     // lleno dentro de la zona ciega
  TEST_ASSERT_FALSE(Tanque::valido(32, 6, 0));     // sin litros
  TEST_ASSERT_FALSE(Tanque::valido(NAN, 6, 8));
}

// La dosis de PAC usa los litros del recipiente configurado.
void test_coag_usa_recipiente_configurado() {
  Tanque antes = tanqueR1;
  tanqueR1 = {32, 6, 8};
  Coagulacion c;
  c.actualizar(300, 50, true, true, false, false, 1000);
  c.actualizar(300, 50, true, true, false, false, 1000 + COAG_MEDIR_MS);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 4, c.litrosLote);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 80, c.mlObjetivo);  // 200 ppm × 4 L / 10 000 ppm
  tanqueR1 = antes;
}

void test_tds() {
  TEST_ASSERT_EQUAL_FLOAT(0, tdsDesdeVoltaje(0));
  // Curva del fabricante a 25 °C: 1 V → (133,42 − 255,86 + 857,39) × 0,5 ≈ 367 ppm
  TEST_ASSERT_FLOAT_WITHIN(1, 367.5f, tdsDesdeVoltaje(1.0f));
  // Más caliente conduce más: a 35 °C la misma tensión son menos ppm
  TEST_ASSERT_TRUE(tdsDesdeVoltaje(1.0f, 35) < tdsDesdeVoltaje(1.0f, 25));
  TEST_ASSERT_EQUAL(0, rangoTds(300));
  TEST_ASSERT_EQUAL(1, rangoTds(800));
  TEST_ASSERT_TRUE(aguaSalobre(1500));
  TEST_ASSERT_FALSE(aguaSalobre(NAN));
}

void test_coag_cancelar_no_dosifica() {
  Coagulacion c;
  c.actualizar(300, 20, true, true, false, false, 1000);
  c.actualizar(300, 20, true, true, false, false, 1000 + COAG_MEDIR_MS);
  TEST_ASSERT_TRUE(c.bombaPac);
  c.cancelar();
  TEST_ASSERT_FALSE(c.bombaPac);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::ESPERA);
  TEST_ASSERT_TRUE(c.pendiente());
}

// ---------- Coagulación ----------
// Lleva un lote de 100 L (20 % de R1) con agua muy turbia hasta DOSIFICANDO.
static unsigned long hastaDosificar(Coagulacion& c, unsigned long t) {
  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::MIDIENDO);
  t += COAG_MEDIR_MS;
  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::DOSIFICANDO);
  return t;
}

void test_coag_ciclo_completo() {
  Coagulacion c;
  unsigned long t = hastaDosificar(c, 1000);
  TEST_ASSERT_FLOAT_WITHIN(1, 2000, c.mlObjetivo);  // 200 ppm × 100 L / 10 000 ppm
  t += 3UL * 60000UL;                               // 2 L a 1 L/min
  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::COAGULANDO);
  t += COAG_RAPIDA_MS; c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::FLOCULANDO);
  t += COAG_LENTA_MS;  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::SEDIMENTANDO);
  t += COAG_SEDIM_MS;  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.lista());
}

// Fallo 2: si R1 se queda sin agua a media dosis, al volver no repite la dosis entera.
void test_coag_sin_doble_dosis() {
  Coagulacion c;
  unsigned long t = hastaDosificar(c, 1000);
  t += 60000;  // 1 L de los 2 L
  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_FLOAT_WITHIN(1, 1000, c.mlAplicados);
  c.actualizar(300, 20, true, false, false, false, t + 1);  // flotador abajo
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::ESPERA);
  TEST_ASSERT_FLOAT_WITHIN(1, 1000, c.mlPrevios());
  t += 1000;
  c.actualizar(300, 20, true, true, false, false, t);  // vuelve el agua
  t += COAG_MEDIR_MS;
  c.actualizar(300, 20, true, true, false, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::DOSIFICANDO);
  TEST_ASSERT_FLOAT_WITHIN(1, 1000, c.mlObjetivo);  // solo lo que faltaba
}

// Fallo 1: un corte a media dosis se guarda como "toda la dosis echada" (nunca se pasa).
void test_coag_respaldo_conservador() {
  Coagulacion c;
  unsigned long t = hastaDosificar(c, 1000);
  RespaldoCoag r = c.respaldo();
  TEST_ASSERT_FLOAT_WITHIN(1, 2000, r.mlPrevios);
  TEST_ASSERT_FLOAT_WITHIN(1, 2000, r.mlTotal);
  TEST_ASSERT_EQUAL(0, r.tratada);

  Coagulacion d;  // tras reiniciar
  d.restaurar(r);
  TEST_ASSERT_FLOAT_WITHIN(1, PAC_TANQUE_ML - 2000, d.mlRestantes());
  d.actualizar(300, 20, true, true, false, false, t);
  d.actualizar(300, 20, true, true, false, false, t + COAG_MEDIR_MS);
  TEST_ASSERT_TRUE(d.estado == EstadoCoag::COAGULANDO);  // no vuelve a echar PAC
}

void test_coag_restaura_tratada() {
  Coagulacion c;
  RespaldoCoag r = {500, 0, 0, 60, 3, 1};
  c.restaurar(r);
  c.actualizar(5, 60, true, true, false, false, 1000);
  TEST_ASSERT_TRUE(c.lista());
  c.actualizar(5, 70, true, true, false, false, 2000);  // entró agua durante el corte
  TEST_ASSERT_FALSE(c.lista());
}

// Fallo 3 (sin válvula): transfiriendo, una subida pequeña de R1 ya es agua cruda.
void test_coag_entrada_durante_transferencia() {
  Coagulacion c;
  RespaldoCoag r = {0, 0, 0, 60, 1, 1};
  c.restaurar(r);
  c.actualizar(5, 55, true, true, true, true, 1000);  // baja al transferir
  TEST_ASSERT_TRUE(c.lista());
  c.actualizar(5, 55 + R1_SUBIDA_TRANSFER, true, true, true, true, 2000);
  TEST_ASSERT_FALSE(c.lista());
}

// ---------- Válvula de entrada ----------
void test_valvula_lote() {
  ControlValvula v;
  v.actualizar(30, true, false, false, 1000);
  TEST_ASSERT_TRUE(v.abierta);
  v.actualizar(30, true, true, false, 2000);  // R1 ocupado: cierra
  TEST_ASSERT_FALSE(v.abierta);

  ControlValvula w;
  w.actualizar(30, true, false, false, 1000);
  w.actualizar(60, true, false, false, 5000);
  bool tratar = w.actualizar(R1_LLENO_MAX, true, false, false, 9000);
  TEST_ASSERT_FALSE(w.abierta);
  TEST_ASSERT_TRUE(tratar);  // entró agua: tratar R1
}

void test_valvula_sin_agua_reintenta() {
  ControlValvula v;
  v.actualizar(30, true, false, false, 1000);
  bool tratar = v.actualizar(30, true, false, false, 1000 + VALVULA_SIN_AGUA_MS);
  TEST_ASSERT_FALSE(v.abierta);
  TEST_ASSERT_FALSE(tratar);
  v.actualizar(30, true, false, false, 2000 + VALVULA_SIN_AGUA_MS);
  TEST_ASSERT_FALSE(v.abierta);  // espera antes de reintentar
  v.actualizar(30, true, false, false, 2000 + VALVULA_SIN_AGUA_MS + VALVULA_REINTENTO_MS);
  TEST_ASSERT_TRUE(v.abierta);
}

void test_valvula_no_abre_si_r2_espera() {
  ControlValvula v;
  v.actualizar(30, true, false, true, 1000);
  TEST_ASSERT_FALSE(v.abierta);
}

// ---------- Cloración ----------
static unsigned long clorarHastaGoteo(Cloracion& c, unsigned long t) {
  c.aguaNueva(300, t);
  t += MEZCLA_MS;
  c.actualizar(7.0f, 0, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCloro::DOSIFICANDO);
  return t;
}

void test_cloro_ciclo() {
  Cloracion c;
  unsigned long t = clorarHastaGoteo(c, 1000);
  TEST_ASSERT_EQUAL_FLOAT(300, c.gotasObjetivo);
  t += 60000; c.actualizar(7.0f, 0, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCloro::CONTACTO);
  t += CLORO_CONTACTO_MS; c.actualizar(7.0f, 0, false, t);
  TEST_ASSERT_TRUE(c.estado == EstadoCloro::LISTA);
}

void test_cloro_turbia_no_clora() {
  Cloracion c;
  c.aguaNueva(100, 0);
  c.actualizar(7.0f, 50, false, MEZCLA_MS);
  TEST_ASSERT_TRUE(c.estado == EstadoCloro::TURBIA);
}

// Corte de luz durante el goteo: no redosifica ni queda en verde.
void test_cloro_corte_en_goteo() {
  Cloracion c;
  clorarHastaGoteo(c, 1000);
  RespaldoCloro r = c.respaldo();
  TEST_ASSERT_EQUAL(FASE_GOTEO, r.fase);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 300, r.gotasTotal);  // inventario: peor caso
  Cloracion d;
  d.restaurar(r, 0);
  TEST_ASSERT_TRUE(d.estado == EstadoCloro::SIN_DATO);
  TEST_ASSERT_EQUAL_FLOAT(0, d.litrosPendientes);
}

// Corte durante el contacto: el tiempo vuelve a empezar.
void test_cloro_corte_en_contacto() {
  Cloracion c;
  unsigned long t = clorarHastaGoteo(c, 1000);
  c.actualizar(7.0f, 0, false, t + 60000);
  Cloracion d;
  d.restaurar(c.respaldo(), 5000);
  TEST_ASSERT_TRUE(d.estado == EstadoCloro::CONTACTO);
  TEST_ASSERT_EQUAL(CLORO_CONTACTO_MS, d.faltaMs(5000));
}

// Corte mientras entraba agua: se recupera desde dónde empezó a llenar.
void test_cloro_corte_llenando() {
  Cloracion c;
  c.inicioLlenado(100);
  c.actualizar(7.0f, 0, true, 1000);
  RespaldoCloro r = c.respaldo();
  TEST_ASSERT_EQUAL(FASE_LLENANDO, r.fase);
  Cloracion d;
  d.restaurar(r, 0);
  TEST_ASSERT_TRUE(d.llenadoInterrumpido());
  d.aguaNueva(400 - d.litrosInicioLlenado(), 0);
  TEST_ASSERT_EQUAL_FLOAT(300, d.litrosPendientes);
  TEST_ASSERT_TRUE(d.estado == EstadoCloro::MEZCLANDO);
}

// ---------- Transferencia ----------
void test_transfer_corte_seguridad() {
  ControlBomba b;
  b.manual(true);
  TEST_ASSERT_TRUE(b.actualizar({50, true, true, "", false, 1000}));
  TEST_ASSERT_FALSE(b.actualizar({NIVEL_SEGURIDAD, true, true, "", false, 2000}));
}

// Modo UNO: el UNO llena y bate; el ESP32 echa el PAC mientras bate y luego sedimenta.
void test_coag_desde_uno() {
  Coagulacion c;
  unsigned long t = 1000;
  c.desdeUno(1, 300, 20, true, false, t);  // llenando
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::ESPERA);
  TEST_ASSERT_FALSE(c.bombaPac);
  c.desdeUno(2, 300, 20, true, false, t += 1000);  // lleno, batiendo: calcula y dosifica
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::DOSIFICANDO);
  TEST_ASSERT_TRUE(c.bombaPac);
  TEST_ASSERT_FLOAT_WITHIN(1, 2000, c.mlObjetivo);  // turbidez × litros, como sin UNO
  c.desdeUno(2, 300, 20, true, false, t += 2UL * 60000UL);  // 2 L a 1 L/min
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::COAGULANDO);
  TEST_ASSERT_FALSE(c.bombaPac);
  TEST_ASSERT_FLOAT_WITHIN(1, 2000, c.mlAplicados);
  c.desdeUno(3, 300, 20, true, false, t += 1000);  // terminó de batir: sedimenta
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::SEDIMENTANDO);
  TEST_ASSERT_FALSE(c.lista());
  c.desdeUno(3, 300, 20, true, false, t + COAG_SEDIM_MS);
  TEST_ASSERT_TRUE(c.lista());
  TEST_ASSERT_EQUAL(1, (int)c.lotes);
  c.desdeUno(3, 300, 20, true, false, t + COAG_SEDIM_MS + 5000);  // no repite el lote
  TEST_ASSERT_TRUE(c.lista());
  c.desdeUno(1, 300, 20, true, false, t + COAG_SEDIM_MS + 9000);  // R1 se llena otra vez
  TEST_ASSERT_FALSE(c.lista());
}

// Modo UNO con agua salobre: no echa PAC.
void test_coag_desde_uno_salobre() {
  Coagulacion c;
  c.desdeUno(1, 300, 20, true, true, 1000);
  c.desdeUno(2, 300, 20, true, true, 2000);
  TEST_ASSERT_TRUE(c.estado == EstadoCoag::ESPERA);
  TEST_ASSERT_FALSE(c.bombaPac);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pac_tabla);
  RUN_TEST(test_cloro_por_ph);
  RUN_TEST(test_tanque_frasco);
  RUN_TEST(test_tanque_validacion);
  RUN_TEST(test_coag_usa_recipiente_configurado);
  RUN_TEST(test_tds);
  RUN_TEST(test_coag_cancelar_no_dosifica);
  RUN_TEST(test_coag_ciclo_completo);
  RUN_TEST(test_coag_sin_doble_dosis);
  RUN_TEST(test_coag_respaldo_conservador);
  RUN_TEST(test_coag_restaura_tratada);
  RUN_TEST(test_coag_entrada_durante_transferencia);
  RUN_TEST(test_valvula_lote);
  RUN_TEST(test_valvula_sin_agua_reintenta);
  RUN_TEST(test_valvula_no_abre_si_r2_espera);
  RUN_TEST(test_cloro_ciclo);
  RUN_TEST(test_cloro_turbia_no_clora);
  RUN_TEST(test_cloro_corte_en_goteo);
  RUN_TEST(test_cloro_corte_en_contacto);
  RUN_TEST(test_cloro_corte_llenando);
  RUN_TEST(test_transfer_corte_seguridad);
  RUN_TEST(test_coag_desde_uno);
  RUN_TEST(test_coag_desde_uno_salobre);
  return UNITY_END();
}
