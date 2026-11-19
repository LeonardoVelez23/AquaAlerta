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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pac_tabla);
  RUN_TEST(test_cloro_por_ph);
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
  return UNITY_END();
}
