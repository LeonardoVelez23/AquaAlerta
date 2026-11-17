// Lógica de control y alertas, separada del hardware (se puede probar en PC).
#pragma once
#include <math.h>

#include "config.h"

// ---------- Conversiones ----------
inline float nivelDesdeDistancia(float distCm) {
  float p = (R2_DIST_VACIO_CM - distCm) / (R2_DIST_VACIO_CM - R2_DIST_LLENO_CM) * 100.0f;
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

inline float nivelR1DesdeDistancia(float distCm) {
  float p = (R1_DIST_VACIO_CM - distCm) / (R1_DIST_VACIO_CM - R1_DIST_LLENO_CM) * 100.0f;
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

inline float ntuDesdeVoltaje(float v) {
  if (v >= 4.2f) return 0;  // sobre el rango de la curva: agua limpia
  float ntu = -1120.4f * v * v + 5742.3f * v - 4352.9f;
  if (v < 2.5f || ntu > 3000) ntu = 3000;  // tope del sensor
  return ntu < 0 ? 0 : ntu;
}

// Recta de calibración con dos buffers (pH 7 y pH 4).
inline float phDesdeVoltaje(float v) {
  float pendiente = (PH_V4 - PH_V7) / 3.0f;  // V por unidad de pH
  float ph = 7.0f + (PH_V7 - v) / pendiente;
  return ph < 0 ? 0 : (ph > 14 ? 14 : ph);
}

// Gotas por litro según el pH: la dosis es fija (MSP); el pH decide si se puede
// clorar. -1 = pH sobre PH_MAX: el cloro no desinfecta bien, hay que corregirlo.
inline float gotasPorLitro(float ph) {
  if (isnan(ph) || ph > PH_MAX) return -1;
  return CLORO_GOTAS_POR_LITRO;
}

// 0 ácido (< 6,5), 1 óptimo (6,5–7,5), 2 aceptable (7,5–8), 3 alto (> 8: no clora)
inline int rangoPh(float ph) {
  if (ph < PH_MIN) return 0;
  if (ph <= PH_OPTIMO_MAX) return 1;
  return ph <= PH_MAX ? 2 : 3;
}

// 0 poco, 1 medio, 2 alto
inline int nivelSedimento(float ntu) {
  if (isnan(ntu) || ntu < SED_MEDIO_NTU) return 0;
  return ntu < SED_ALTO_NTU ? 1 : 2;
}

// Dosis de PAC (ppm) según la turbidez, interpolando la tabla de la prueba de jarras.
// 0 = agua clara, no hace falta coagular.
inline float pacPpm(float ntu) {
  if (isnan(ntu) || ntu < COAG_MIN_NTU) return 0;
  if (ntu <= PAC_TABLA_NTU[0]) return PAC_TABLA_PPM[0];
  for (int i = 1; i < PAC_PUNTOS; i++) {
    if (ntu <= PAC_TABLA_NTU[i]) {
      float f = (ntu - PAC_TABLA_NTU[i - 1]) / (PAC_TABLA_NTU[i] - PAC_TABLA_NTU[i - 1]);
      return PAC_TABLA_PPM[i - 1] + f * (PAC_TABLA_PPM[i] - PAC_TABLA_PPM[i - 1]);
    }
  }
  return PAC_TABLA_PPM[PAC_PUNTOS - 1];  // tope: turbidez fuera de la tabla
}
inline bool pacFueraDeTabla(float ntu) { return !isnan(ntu) && ntu > PAC_TABLA_NTU[PAC_PUNTOS - 1]; }

// C1·V1 = C2·V2 → V1 = C2·V2 / C1. ml de solución madre para `litros` de agua a `ppm`.
inline float mlPac(float ppm, float litros) { return ppm * litros * 1000.0f / PAC_STOCK_PPM; }

// RPM → ciclo de trabajo PWM (lazo abierto, calibrado con AGIT_RPM_MAX).
inline int rpmAPwm(int rpm) {
  int maxPwm = (1 << AGIT_PWM_BITS) - 1;
  int d = (int)lroundf((float)rpm / AGIT_RPM_MAX * maxPwm);
  return d < 0 ? 0 : (d > maxPwm ? maxPwm : d);
}

template <int N>
class MediaMovil {
 public:
  float agregar(float v) {
    datos_[pos_] = v;
    pos_ = (pos_ + 1) % N;
    if (n_ < N) n_++;
    float s = 0;
    for (int i = 0; i < n_; i++) s += datos_[i];
    return s / n_;
  }

 private:
  float datos_[N] = {};
  int pos_ = 0, n_ = 0;
};

// ---------- Coagulación y floculación con PAC en R1 ----------
//  agua nueva → MIDIENDO (agita y mide) → DOSIFICANDO (PAC) → COAGULANDO (rápido)
//    → FLOCULANDO (lento) → SEDIMENTANDO (quieto) → LISTA → la purga saca el lodo
//  agua clara (< COAG_MIN_NTU) → LISTA sin PAC
enum class EstadoCoag { ESPERA, MIDIENDO, DOSIFICANDO, COAGULANDO, FLOCULANDO, SEDIMENTANDO, LISTA, SIN_PAC };

inline const char* textoCoag(EstadoCoag e) {
  switch (e) {
    case EstadoCoag::ESPERA:       return "Agua por tratar";
    case EstadoCoag::MIDIENDO:     return "Midiendo R1";
    case EstadoCoag::DOSIFICANDO:  return "Dosificando PAC";
    case EstadoCoag::COAGULANDO:   return "Coagulando";
    case EstadoCoag::FLOCULANDO:   return "Floculando";
    case EstadoCoag::SEDIMENTANDO: return "Sedimentando";
    case EstadoCoag::SIN_PAC:      return "Falta PAC";
    default:                       return "R1 tratada";
  }
}

class Coagulacion {
 public:
  EstadoCoag estado = EstadoCoag::ESPERA;
  bool bombaPac = false;
  int rpm = 0;
  bool sinPac = false;        // última vez el agua no necesitó PAC
  bool sinNivel = false;      // sin lectura de R1: se saltó la dosis
  bool fueraTabla = false;    // turbidez mayor que la tabla: dosis tope
  float ntuMedida = -1, ppm = 0, litrosLote = 0;
  float mlObjetivo = 0, mlAplicados = 0, mlTotal = 0;
  unsigned long lotes = 0;    // lotes coagulados

  float mlRestantes() const {
    float r = PAC_TANQUE_ML - (mlTotal - mlRecarga_);
    return r < 0 ? 0 : r;
  }
  float pctRestante() const { return mlRestantes() / PAC_TANQUE_ML * 100.0f; }
  void recargado() {
    mlRecarga_ = mlTotal;
    if (estado == EstadoCoag::SIN_PAC) estado = EstadoCoag::ESPERA;
  }
  // Botón "Tratar R1 ahora".
  void iniciar() {
    if (!ocupado()) { pendiente_ = true; estado = EstadoCoag::ESPERA; }
  }
  bool pendiente() const { return pendiente_; }
  // R1 se está tratando: no purgar ni transferir.
  bool ocupado() const {
    return estado == EstadoCoag::MIDIENDO || estado == EstadoCoag::DOSIFICANDO ||
           estado == EstadoCoag::COAGULANDO || estado == EstadoCoag::FLOCULANDO ||
           estado == EstadoCoag::SEDIMENTANDO;
  }
  bool lista() const { return estado == EstadoCoag::LISTA && !pendiente_; }
  unsigned long faltaMs(unsigned long ahora) const {
    unsigned long d = ahora - desde_, total;
    switch (estado) {
      case EstadoCoag::MIDIENDO:     total = COAG_MEDIR_MS; break;
      case EstadoCoag::COAGULANDO:   total = COAG_RAPIDA_MS; break;
      case EstadoCoag::FLOCULANDO:   total = COAG_LENTA_MS; break;
      case EstadoCoag::SEDIMENTANDO: total = COAG_SEDIM_MS; break;
      default:                       return 0;
    }
    return d < total ? total - d : 0;
  }

  // ntu: turbidez de R1; nivel/nivelOk: ultrasonido de R1; bloqueado: purga o transferencia activas.
  void actualizar(float ntu, float nivel, bool nivelOk, bool hayAgua, bool bloqueado, unsigned long ahora) {
    if (bombaPac) {
      float ml = (ahora - tUltimo_) / 60000.0f * PAC_CAUDAL_ML_MIN;
      mlAplicados += ml;
      mlTotal += ml;
    }
    tUltimo_ = ahora;

    // ¿Entró agua nueva? Se compara con el nivel más bajo desde el último lote.
    if (nivelOk && !ocupado()) {
      if (!hayBase_ || nivel < base_) { base_ = nivel; hayBase_ = true; }
      if (nivel >= base_ + R1_SUBIDA_LOTE) {
        pendiente_ = true;
        base_ = nivel;
        if (estado == EstadoCoag::LISTA) estado = EstadoCoag::ESPERA;
      }
    }

    if (!hayAgua && ocupado()) estado = EstadoCoag::ESPERA;  // se vació: reintenta luego

    switch (estado) {
      case EstadoCoag::ESPERA:
        if (pendiente_ && hayAgua && !bloqueado) cambiar(EstadoCoag::MIDIENDO, ahora);
        break;
      case EstadoCoag::MIDIENDO:
        if (ahora - desde_ >= COAG_MEDIR_MS) calcularDosis(ntu, nivel, nivelOk, ahora);
        break;
      case EstadoCoag::DOSIFICANDO:
        if (mlAplicados >= mlObjetivo) cambiar(EstadoCoag::COAGULANDO, ahora);
        else if (mlRestantes() <= 0) estado = EstadoCoag::SIN_PAC;
        break;
      case EstadoCoag::COAGULANDO:
        if (ahora - desde_ >= COAG_RAPIDA_MS) cambiar(EstadoCoag::FLOCULANDO, ahora);
        break;
      case EstadoCoag::FLOCULANDO:
        if (ahora - desde_ >= COAG_LENTA_MS) cambiar(EstadoCoag::SEDIMENTANDO, ahora);
        break;
      case EstadoCoag::SEDIMENTANDO:
        if (ahora - desde_ >= COAG_SEDIM_MS) { lotes++; terminarLote(nivel, nivelOk); }
        break;
      default:
        break;
    }

    bombaPac = estado == EstadoCoag::DOSIFICANDO;
    switch (estado) {
      case EstadoCoag::MIDIENDO:
      case EstadoCoag::DOSIFICANDO:
      case EstadoCoag::COAGULANDO: rpm = AGIT_RPM_RAPIDA; break;
      case EstadoCoag::FLOCULANDO: rpm = AGIT_RPM_LENTA; break;
      default:                     rpm = 0;
    }
  }

 private:
  bool pendiente_ = true;  // al encender no se sabe si R1 está tratada
  bool hayBase_ = false;
  float base_ = 0, mlRecarga_ = 0;
  unsigned long desde_ = 0, tUltimo_ = 0;

  void cambiar(EstadoCoag e, unsigned long ahora) { estado = e; desde_ = ahora; }

  void terminarLote(float nivel, bool nivelOk) {
    pendiente_ = false;
    estado = EstadoCoag::LISTA;
    if (nivelOk) base_ = nivel;
  }

  void calcularDosis(float ntu, float nivel, bool nivelOk, unsigned long ahora) {
    ntuMedida = ntu;
    ppm = pacPpm(ntu);
    fueraTabla = pacFueraDeTabla(ntu);
    sinPac = ppm <= 0;
    sinNivel = !nivelOk;
    if (sinPac || sinNivel) {  // agua clara, o no se pueden calcular los litros
      mlObjetivo = 0;
      terminarLote(nivel, nivelOk);
      return;
    }
    litrosLote = nivel / 100.0f * R1_CAPACIDAD_L;
    mlObjetivo = mlPac(ppm, litrosLote);
    mlAplicados = 0;
    if (mlRestantes() < mlObjetivo) { estado = EstadoCoag::SIN_PAC; return; }
    cambiar(EstadoCoag::DOSIFICANDO, ahora);
  }
};

// ---------- Purga de sedimentos en R1 ----------
enum class EstadoPurga { LIMPIO, PULSO, PAUSA };

class ControlPurga {
 public:
  bool automatico = true;
  bool encendida = false;
  bool falla = false;  // no logra limpiar tras PURGA_MAX_PULSOS
  EstadoPurga estado = EstadoPurga::LIMPIO;
  int pulso = 0;
  const char* motivo = "Sin sedimentos";

  // Métricas
  unsigned long purgas = 0;  // purgas terminadas con éxito
  unsigned long msEncendida = 0;
  float ntuAntes = -1, ntuDespues = -1;  // última purga

  void modoAutomatico() { automatico = true; pedidoManual_ = false; }
  void manual(bool encender) { automatico = false; pedidoManual_ = encender; estado = EstadoPurga::LIMPIO; }
  void reiniciarFalla() { falla = false; estado = EstadoPurga::LIMPIO; }
  unsigned long msTotal(unsigned long ahora) const { return msEncendida + (encendida ? ahora - tOn_ : 0); }
  float litrosPurgados(unsigned long ahora) const { return msTotal(ahora) / 60000.0f * PURGA_CAUDAL_LPM; }

  // R1 ya no tiene sedimentos: se puede pasar el agua a R2.
  bool sinSedimentos(float ntu) const {
    return !falla && !encendida && estado == EstadoPurga::LIMPIO && !isnan(ntu) && ntu < SED_MEDIO_NTU;
  }

  bool actualizar(float ntu, bool hayAgua, bool transfiriendo, unsigned long ahora) {
    bool quiere = false;
    if (falla) {
      motivo = "Purga sin efecto";
    } else if (!automatico) {
      quiere = pedidoManual_;
      motivo = quiere ? "Manual: ON" : "Manual: OFF";
    } else {
      switch (estado) {
        case EstadoPurga::LIMPIO:
          if (!isnan(ntu) && ntu >= SED_MEDIO_NTU && !transfiriendo && hayAgua) {
            estado = EstadoPurga::PULSO;
            pulso = 1;
            desde_ = ahora;
            ntuInicio_ = ntu;
          }
          motivo = estado != EstadoPurga::LIMPIO ? "Purgando"
                   : (ntu >= SED_MEDIO_NTU ? "Con sedimentos" : "Sin sedimentos");
          break;
        case EstadoPurga::PULSO:
          if (ahora - desde_ >= PURGA_PULSO_MS) { estado = EstadoPurga::PAUSA; desde_ = ahora; }
          motivo = "Purgando";
          break;
        case EstadoPurga::PAUSA:  // deja asentar y vuelve a medir
          motivo = "Asentando";
          if (ahora - desde_ >= PURGA_PAUSA_MS) {
            if (!isnan(ntu) && ntu <= SED_LIMPIO_NTU) {
              estado = EstadoPurga::LIMPIO;
              purgas++;
              ntuAntes = ntuInicio_;
              ntuDespues = ntu;
              motivo = "Sin sedimentos";
            } else if (pulso >= PURGA_MAX_PULSOS) {
              falla = true;
              estado = EstadoPurga::LIMPIO;
              motivo = "Purga sin efecto";
            } else {
              pulso++;
              estado = EstadoPurga::PULSO;
              desde_ = ahora;
            }
          }
          break;
      }
      quiere = estado == EstadoPurga::PULSO;
    }
    if (!hayAgua) { quiere = false; motivo = "R1 sin agua"; }

    if (quiere && !encendida) tOn_ = ahora;
    if (!quiere && encendida) msEncendida += ahora - tOn_;
    encendida = quiere;
    return encendida;
  }

 private:
  bool pedidoManual_ = false;
  unsigned long desde_ = 0, tOn_ = 0;
  float ntuInicio_ = 0;
};

// ---------- Bomba de transferencia (R1 → filtro → R2) ----------
enum class Falla { NINGUNA, SIN_SUBIR, TIEMPO_MAX, AGUA_TURBIA };

inline const char* textoFalla(Falla f) {
  switch (f) {
    case Falla::SIN_SUBIR:   return "Filtro tapado?";
    case Falla::TIEMPO_MAX:  return "Transf. t. max";
    case Falla::AGUA_TURBIA: return "Filtro no limpia";
    default:                 return "";
  }
}

struct EntradaBomba {
  float nivel;           // % de R2
  bool nivelOk;
  bool origen;           // R1 puede entregar agua
  const char* porQueNo;  // motivo cuando origen == false
  bool aguaTurbia;       // la turbidez de R2 supera TURB_R2_MAX_CLORO
  unsigned long ahora;
};

class ControlBomba {
 public:
  bool automatico = true;
  bool encendida = false;
  Falla falla = Falla::NINGUNA;
  const char* motivo = "Apagada";

  // Métricas
  unsigned long ciclos = 0;
  unsigned long msEncendida = 0;
  float litrosBombeados = 0;           // medidos por la subida de R2
  float ultimaRespuestaS = -1;         // encendido → R2 sube SUBIDA_MINIMA
  unsigned long llenadosCompletos = 0;
  unsigned long cortesSeguridad = 0;

  void modoAutomatico() { automatico = true; pedidoManual_ = false; }
  void manual(bool encender) { automatico = false; pedidoManual_ = encender; }
  void reiniciarFalla() { falla = Falla::NINGUNA; }
  unsigned long msEncendidaTotal(unsigned long ahora) const {
    return msEncendida + (encendida ? ahora - desde_ : 0);
  }

  bool actualizar(const EntradaBomba& e) {
    bool quiere;
    if (automatico) {
      quiere = encendida ? e.nivel < R2_APAGAR : e.nivel <= R2_ENCENDER;
      motivo = quiere ? "Auto: pasando agua" : "Auto: en reposo";
      if (encendida && !quiere && e.nivelOk) llenadosCompletos++;
    } else {
      quiere = pedidoManual_;
      motivo = quiere ? "Manual: ON" : "Manual: OFF";
    }

    // Protecciones (valen también en modo manual).
    if (encendida) revisarFuncionamiento(e);
    if (falla != Falla::NINGUNA) { quiere = false; motivo = textoFalla(falla); }
    if (!e.nivelOk) { quiere = false; motivo = "Sin lectura R2"; }
    if (!e.origen)  { quiere = false; motivo = e.porQueNo; }
    if (e.nivelOk && e.nivel >= NIVEL_SEGURIDAD) {
      if (quiere && encendida) cortesSeguridad++;
      quiere = false;
      pedidoManual_ = false;
      motivo = "Corte: R2 lleno";
    }
    if (quiere && !encendida && huboApagado_ && e.ahora - hastaApagado_ < BOMBA_MIN_APAGADA_MS) {
      quiere = false;
      motivo = "Reposo de bomba";
    }

    if (quiere && !encendida) encender(e);
    else if (!quiere && encendida) apagar(e);
    return encendida;
  }

 private:
  bool pedidoManual_ = false;
  bool esperandoRespuesta_ = false, huboApagado_ = false;
  unsigned long desde_ = 0, hastaApagado_ = 0;
  float nivelInicial_ = 0;

  void revisarFuncionamiento(const EntradaBomba& e) {
    unsigned long t = e.ahora - desde_;
    if (esperandoRespuesta_ && e.nivelOk && e.nivel >= nivelInicial_ + SUBIDA_MINIMA) {
      esperandoRespuesta_ = false;
      ultimaRespuestaS = t / 1000.0f;
    }
    // R2 no sube: filtro tapado, manguera doblada o bomba en seco.
    if (esperandoRespuesta_ && t >= BOMBA_SIN_SUBIR_MS) falla = Falla::SIN_SUBIR;
    if (t >= BOMBA_MAX_MS) falla = Falla::TIEMPO_MAX;
    // El agua sigue llegando turbia: el filtro no está reteniendo.
    if (!e.aguaTurbia) hayTurbia_ = false;
    else if (!hayTurbia_) { hayTurbia_ = true; turbiaDesde_ = e.ahora; }
    else if (e.ahora - turbiaDesde_ >= FILTRO_TURBIO_MS) falla = Falla::AGUA_TURBIA;
  }

  bool hayTurbia_ = false;
  unsigned long turbiaDesde_ = 0;

  void encender(const EntradaBomba& e) {
    encendida = true;
    hayTurbia_ = false;
    desde_ = e.ahora;
    nivelInicial_ = e.nivel;
    esperandoRespuesta_ = true;
    ciclos++;
  }

  void apagar(const EntradaBomba& e) {
    encendida = false;
    msEncendida += e.ahora - desde_;
    if (e.nivelOk && e.nivel > nivelInicial_)
      litrosBombeados += (e.nivel - nivelInicial_) / 100.0f * R2_CAPACIDAD_L;
    huboApagado_ = true;
    hastaApagado_ = e.ahora;
  }
};

// ---------- Cloración de R2 según el pH ----------
//  agua nueva → MEZCLANDO (espera) → mide pH → DOSIFICANDO (gotero) → CONTACTO → LISTA
//                                       └─ pH fuera de rango → PH_FUERA (no clora, avisa)
enum class EstadoCloro { SIN_DATO, LLENANDO, MEZCLANDO, TURBIA, PH_FUERA, SIN_CLORO, DOSIFICANDO, CONTACTO, LISTA };

// Agua apta para clorar: clara (OMS < 5 NTU).
inline bool claraParaClorar(float ntu) { return !isnan(ntu) && ntu <= TURB_R2_MAX_CLORO; }

inline const char* textoCloro(EstadoCloro e) {
  switch (e) {
    case EstadoCloro::SIN_DATO:    return "Sin registro";
    case EstadoCloro::LLENANDO:    return "Entra agua";
    case EstadoCloro::MEZCLANDO:   return "Midiendo pH";
    case EstadoCloro::TURBIA:      return "R2 turbia";
    case EstadoCloro::PH_FUERA:    return "pH fuera rango";
    case EstadoCloro::SIN_CLORO:   return "Falta cloro";
    case EstadoCloro::DOSIFICANDO: return "Aplicando cloro";
    case EstadoCloro::CONTACTO:    return "Clorando:espere";
    default:                       return "Agua lista";
  }
}

class Cloracion {
 public:
  EstadoCloro estado = EstadoCloro::SIN_DATO;
  bool goteoAbierto = false;
  float litrosPendientes = 0;  // agua nueva sin clorar
  float gotasObjetivo = 0, gotasAplicadas = 0, gotasTotal = 0;
  float phDosis = -1;          // pH con el que se calculó la última dosis
  float gotasPorLitroUsadas = 0;
  unsigned long dosis = 0;

  float mlRestantes() const {
    float r = GOTERO_CAPACIDAD_ML - (gotasTotal - gotasRecarga_) / GOTAS_POR_ML;
    return r < 0 ? 0 : r;
  }
  float pctRestante() const { return mlRestantes() / GOTERO_CAPACIDAD_ML * 100.0f; }
  void recargado(unsigned long ahora) {
    gotasRecarga_ = gotasTotal;
    if (estado == EstadoCloro::SIN_CLORO) iniciarMezcla(ahora);
  }
  // Ocupado: R2 debe quedarse quieto (no se transfiere).
  bool ocupado() const { return estado == EstadoCloro::MEZCLANDO || estado == EstadoCloro::DOSIFICANDO; }
  unsigned long faltaMs(unsigned long ahora) const {
    unsigned long d = ahora - desde_;
    if (estado == EstadoCloro::MEZCLANDO) return d < MEZCLA_MS ? MEZCLA_MS - d : 0;
    if (estado == EstadoCloro::CONTACTO) return d < CLORO_CONTACTO_MS ? CLORO_CONTACTO_MS - d : 0;
    return 0;
  }

  // Terminó de entrar agua a R2.
  void aguaNueva(float litros, unsigned long ahora) {
    if (litros > 0) litrosPendientes += litros;
    if (litrosPendientes > 0) iniciarMezcla(ahora);
    else if (estado == EstadoCloro::LLENANDO) estado = estadoPrevio_;  // no entró agua
  }
  // Botón "Clorar todo R2" (por ejemplo, después de un corte de luz).
  void clorarTodo(float litrosR2, unsigned long ahora) {
    litrosPendientes = litrosR2;
    if (litrosPendientes > 0) iniciarMezcla(ahora);
  }

  bool actualizar(float ph, float ntu, bool transfiriendo, unsigned long ahora) {
    if (goteoAbierto) {
      float g = (ahora - tUltimo_) / 60000.0f * GOTEO_GOTAS_MIN;
      gotasAplicadas += g;
      gotasTotal += g;
    }
    tUltimo_ = ahora;

    if (transfiriendo) {
      if (estado != EstadoCloro::LLENANDO) estadoPrevio_ = estado;
      estado = EstadoCloro::LLENANDO;
    } else {
      switch (estado) {
        case EstadoCloro::MEZCLANDO:
          if (ahora - desde_ >= MEZCLA_MS) calcularDosis(ph, ntu, ahora);
          break;
        case EstadoCloro::TURBIA:    // espera a que se asiente o limpien el filtro
        case EstadoCloro::PH_FUERA:  // espera a que corrijan el pH
          if (claraParaClorar(ntu) && gotasPorLitro(ph) > 0) calcularDosis(ph, ntu, ahora);
          break;
        case EstadoCloro::DOSIFICANDO:
          if (gotasAplicadas >= gotasObjetivo) {
            litrosPendientes = 0;
            dosis++;
            estado = EstadoCloro::CONTACTO;
            desde_ = ahora;
          } else if (mlRestantes() <= 0) {
            estado = EstadoCloro::SIN_CLORO;
          }
          break;
        case EstadoCloro::CONTACTO:
          if (ahora - desde_ >= CLORO_CONTACTO_MS) estado = EstadoCloro::LISTA;
          break;
        default:
          break;
      }
    }
    goteoAbierto = estado == EstadoCloro::DOSIFICANDO;
    return goteoAbierto;
  }

 private:
  unsigned long desde_ = 0, tUltimo_ = 0;
  float gotasRecarga_ = 0;
  EstadoCloro estadoPrevio_ = EstadoCloro::SIN_DATO;

  void iniciarMezcla(unsigned long ahora) {
    estado = EstadoCloro::MEZCLANDO;
    desde_ = ahora;
  }

  void calcularDosis(float ph, float ntu, unsigned long ahora) {
    // Primero filtrar (MSP): el cloro no desinfecta bien el agua turbia.
    if (!claraParaClorar(ntu)) { estado = EstadoCloro::TURBIA; return; }
    float g = gotasPorLitro(ph);
    if (g < 0) { estado = EstadoCloro::PH_FUERA; return; }
    gotasPorLitroUsadas = g;
    phDosis = ph;
    gotasObjetivo = litrosPendientes * g;
    gotasAplicadas = 0;
    if (mlRestantes() * GOTAS_POR_ML < gotasObjetivo) { estado = EstadoCloro::SIN_CLORO; return; }
    estado = EstadoCloro::DOSIFICANDO;
    desde_ = ahora;
  }
};

// ---------- Alertas ----------
enum Alerta { VERDE, AMARILLO, ROJO };

struct Diagnostico {
  Alerta alerta;
  const char* causa;  // ≤16 caracteres (LCD)
};

struct EstadoSistema {
  bool r2Ok;
  float r2;   // %
  float ph;
  float ntuR2;
  bool r1Agua;
  float ntuR1;
  bool r1NivelOk;
  const Coagulacion* coag;
  const ControlPurga* purga;
  const ControlBomba* transfer;
  const Cloracion* cloro;
};

inline Diagnostico diagnosticar(const EstadoSistema& s) {
  EstadoCloro c = s.cloro->estado;
  if (!s.r2Ok) return {ROJO, "Falla sensor R2"};
  if (s.transfer->falla != Falla::NINGUNA) return {ROJO, textoFalla(s.transfer->falla)};
  if (s.purga->falla) return {ROJO, "Purga sin efecto"};
  if (s.r2 >= NIVEL_SEGURIDAD) return {ROJO, "R2: desborde"};
  if (s.r2 <= NIVEL_CRITICO) return {ROJO, "R2 casi vacio"};
  if (c == EstadoCloro::SIN_CLORO) return {ROJO, "Sin cloro"};
  if (s.coag->estado == EstadoCoag::SIN_PAC) return {ROJO, "Sin PAC"};
  if (c == EstadoCloro::TURBIA || !claraParaClorar(s.ntuR2)) return {AMARILLO, "R2 turbia:filtro"};
  if (c == EstadoCloro::PH_FUERA) return {AMARILLO, "pH alto:corregir"};
  if (rangoPh(s.ph) == 0) return {AMARILLO, "pH bajo (acido)"};
  if (s.cloro->pctRestante() < GOTERO_AVISO_PCT) return {AMARILLO, "Recargar cloro"};
  if (s.coag->pctRestante() < PAC_AVISO_PCT) return {AMARILLO, "Recargar PAC"};
  if (!s.r1NivelOk) return {AMARILLO, "Falla nivel R1"};
  if (s.coag->ocupado()) return {AMARILLO, textoCoag(s.coag->estado)};
  if (s.purga->encendida || s.purga->estado != EstadoPurga::LIMPIO) return {AMARILLO, "Purgando lodo"};
  if (!s.r1Agua) return {AMARILLO, "R1 sin agua"};
  if (nivelSedimento(s.ntuR1) > 0) return {AMARILLO, "R1 con lodo"};
  if (c != EstadoCloro::LISTA) return {AMARILLO, c == EstadoCloro::SIN_DATO ? "R2 sin clorar?" : textoCloro(c)};
  if (s.r2 <= R2_ENCENDER) return {AMARILLO, "R2 nivel bajo"};
  return {VERDE, "Agua lista"};
}
