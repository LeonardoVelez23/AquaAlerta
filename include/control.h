// Lógica de control y alertas, separada del hardware (se puede probar en PC).
#pragma once
#include <math.h>
#include <string.h>

#include "config.h"

// ---------- Conversiones ----------
inline float nivelDesdeDistancia(float distCm, const Reservorio& r) {
  float p = (r.distVacioCm - distCm) / (r.distVacioCm - r.distLlenoCm) * 100.0f;
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

inline float ntuDesdeVoltaje(float v) {
  if (v >= 4.2f) return 0;  // sobre el rango de la curva: agua limpia
  float ntu = -1120.4f * v * v + 5742.3f * v - 4352.9f;
  if (v < 2.5f || ntu > 3000) ntu = 3000;  // tope del sensor
  return ntu < 0 ? 0 : ntu;
}

template <int N>
class MediaMovil {
 public:
  float agregar(float v) {
    datos_[pos_] = v;
    pos_ = (pos_ + 1) % N;
    if (n_ < N) n_++;
    return valor();
  }
  float valor() const {
    if (!n_) return NAN;
    float s = 0;
    for (int i = 0; i < n_; i++) s += datos_[i];
    return s / n_;
  }

 private:
  float datos_[N] = {};
  int pos_ = 0, n_ = 0;
};

// Turbidez con histéresis: 0 limpia, 1 turbia, 2 muy turbia.
inline int nivelTurbidez(float ntu, int previo) {
  if (isnan(ntu)) return previo;
  float h = TURB_HISTERESIS_NTU;
  if (ntu >= TURB_ALTA_NTU || (previo == 2 && ntu >= TURB_ALTA_NTU - h)) return 2;
  if (ntu >= TURB_PRECAUCION_NTU || (previo >= 1 && ntu >= TURB_PRECAUCION_NTU - h)) return 1;
  return 0;
}

// ---------- Bomba genérica: llena un destino desde un origen ----------
enum class Falla { NINGUNA, SIN_SUBIR, TIEMPO_MAX };

struct ParamBomba {
  float encender, apagar;  // % del destino (modo automático, con histéresis)
  float capacidadL;        // del destino, para contar litros
  const char* txtSinSubir; // texto de la falla "el destino no sube" (≤16 car. para el LCD)
  const char* txtTiempo;
};

struct EntradaBomba {
  float nivel;            // % del destino
  bool nivelOk;
  bool origen;            // el origen puede entregar agua
  const char* porQueNo;   // motivo cuando origen == false
  unsigned long ahora;
};

class ControlBomba {
 public:
  explicit ControlBomba(const ParamBomba& p) : p_(p) {}

  bool automatico = true;
  bool encendida = false;
  Falla falla = Falla::NINGUNA;
  const char* motivo = "Apagada";

  // Métricas
  unsigned long ciclos = 0;
  unsigned long msEncendida = 0;       // ciclos cerrados
  float litrosBombeados = 0;           // medidos por la subida del destino
  float ultimaRespuestaS = -1;         // encendido → el destino sube SUBIDA_MINIMA
  unsigned long llenadosCompletos = 0; // llegó a "apagar" sin desbordar
  unsigned long cortesSeguridad = 0;   // cortes por NIVEL_SEGURIDAD

  const char* textoFalla() const {
    return falla == Falla::SIN_SUBIR ? p_.txtSinSubir : (falla == Falla::TIEMPO_MAX ? p_.txtTiempo : "");
  }
  void modoAutomatico() { automatico = true; pedidoManual_ = false; }
  void manual(bool encender) { automatico = false; pedidoManual_ = encender; }
  void reiniciarFalla() { falla = Falla::NINGUNA; }
  unsigned long msEncendidaTotal(unsigned long ahora) const {
    return msEncendida + (encendida ? ahora - desde_ : 0);
  }

  // Devuelve true si el relé debe quedar encendido.
  bool actualizar(const EntradaBomba& e) {
    bool quiere;
    if (automatico) {
      quiere = encendida ? e.nivel < p_.apagar : e.nivel <= p_.encender;
      motivo = quiere ? "Auto: bombeando" : "Auto: en reposo";
      if (encendida && !quiere && e.nivelOk) llenadosCompletos++;
    } else {
      quiere = pedidoManual_;
      motivo = quiere ? "Manual: ON" : "Manual: OFF";
    }

    // Protecciones (valen también en modo manual).
    if (encendida) revisarFuncionamiento(e);
    if (falla != Falla::NINGUNA) { quiere = false; motivo = textoFalla(); }
    if (!e.nivelOk) { quiere = false; motivo = "Sin lectura nivel"; }
    if (!e.origen)  { quiere = false; motivo = e.porQueNo; }
    if (e.nivelOk && e.nivel >= NIVEL_SEGURIDAD) {
      if (quiere && encendida) cortesSeguridad++;
      quiere = false;
      pedidoManual_ = false;
      motivo = "Corte: lleno";
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
  ParamBomba p_;
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
    // No sube: bomba en seco, tubo roto o filtro tapado.
    if (esperandoRespuesta_ && t >= BOMBA_SIN_SUBIR_MS) falla = Falla::SIN_SUBIR;
    if (t >= BOMBA_MAX_MS) falla = Falla::TIEMPO_MAX;
  }

  void encender(const EntradaBomba& e) {
    encendida = true;
    desde_ = e.ahora;
    nivelInicial_ = e.nivel;
    esperandoRespuesta_ = true;
    ciclos++;
  }

  void apagar(const EntradaBomba& e) {
    encendida = false;
    msEncendida += e.ahora - desde_;
    if (e.nivelOk && e.nivel > nivelInicial_)
      litrosBombeados += (e.nivel - nivelInicial_) / 100.0f * p_.capacidadL;
    huboApagado_ = true;
    hastaApagado_ = e.ahora;
  }
};

// ---------- Sedimentación en R1 ----------
// Cuenta el reposo desde la última vez que entró agua (por la bomba o por otro
// lado: tanquero, lluvia). Mientras no termine, no se transfiere a R2.
class Sedimentacion {
 public:
  void actualizar(float nivel, bool nivelOk, bool entrandoAgua, unsigned long ahora) {
    if (!iniciado_) { desde_ = ahora; iniciado_ = true; }
    if (!nivelOk) return;
    if (entrandoAgua || isnan(ref_)) { ref_ = nivel; if (entrandoAgua) desde_ = ahora; return; }
    if (nivel < ref_) ref_ = nivel;  // se sacó agua: no revuelve
    else if (nivel > ref_ + SUBIDA_MINIMA) { ref_ = nivel; desde_ = ahora; }  // entró agua
  }
  bool lista(unsigned long ahora) const { return iniciado_ && ahora - desde_ >= SEDIMENTACION_MS; }
  unsigned long faltaMs(unsigned long ahora) const {
    return lista(ahora) ? 0 : SEDIMENTACION_MS - (ahora - desde_);
  }

 private:
  bool iniciado_ = false;
  unsigned long desde_ = 0;
  float ref_ = NAN;
};

// ---------- Gotero de cloro en R2 ----------
// Se abre mientras pasa agua a R2: la dosis queda proporcional a los litros.
class Goteo {
 public:
  bool abierto = false;
  float gotasTotal = 0;
  float gotasCiclo = 0;  // del ciclo en curso o del último
  float dosisPct = -1;   // último ciclo: gotas reales / gotas necesarias × 100
  unsigned long ultimoCierre = 0;
  bool huboGoteo = false;

  float mlRestantes() const {
    float r = GOTERO_CAPACIDAD_ML - (gotasTotal - gotasRecarga_) / GOTAS_POR_ML;
    return r < 0 ? 0 : r;
  }
  float pctRestante() const { return mlRestantes() / GOTERO_CAPACIDAD_ML * 100.0f; }
  void recargado() { gotasRecarga_ = gotasTotal; }

  // Tiempo de contacto cumplido desde la última dosis.
  bool contactoListo(unsigned long ahora) const {
    return !abierto && (!huboGoteo || ahora - ultimoCierre >= CLORO_CONTACTO_MS);
  }
  unsigned long contactoFaltaMs(unsigned long ahora) const {
    if (abierto) return CLORO_CONTACTO_MS;
    return contactoListo(ahora) ? 0 : CLORO_CONTACTO_MS - (ahora - ultimoCierre);
  }
  bool dosisFueraDeRango() const {
    return dosisPct >= 0 && fabsf(dosisPct - 100.0f) > DOSIS_TOLERANCIA_PCT;
  }

  // transfiriendo: la bomba R1→R2 está encendida. Devuelve el estado del relé.
  bool actualizar(bool transfiriendo, unsigned long ahora) {
    if (abierto) {
      float g = (ahora - tUltimo_) / 60000.0f * GOTEO_GOTAS_MIN;
      gotasTotal += g;
      gotasCiclo += g;
    }
    tUltimo_ = ahora;
    bool quiere = transfiriendo && mlRestantes() > 0;
    if (quiere && !abierto) gotasCiclo = 0;
    if (!quiere && abierto) { ultimoCierre = ahora; huboGoteo = true; }
    abierto = quiere;
    return abierto;
  }

  // Al terminar una transferencia: compara lo que goteó con lo que hacía falta.
  void cerrarCiclo(float litrosEntraron) {
    float necesarias = litrosEntraron * CLORO_GOTAS_POR_LITRO;
    dosisPct = necesarias > 0 ? gotasCiclo / necesarias * 100.0f : -1;
  }

 private:
  unsigned long tUltimo_ = 0;
  float gotasRecarga_ = 0;
};

// ---------- Alertas ----------
enum Alerta { VERDE, AMARILLO, ROJO };

struct Diagnostico {
  Alerta alerta;
  const char* causa;  // ≤16 caracteres (LCD)
};

struct EstadoSistema {
  bool r1Ok, r2Ok;
  float r1, r2;            // %
  int turbR2;              // 0/1/2
  bool origen;
  const ControlBomba* llenado;
  const ControlBomba* transfer;
  const Goteo* goteo;
  unsigned long ahora;
};

inline Diagnostico diagnosticar(const EstadoSistema& s) {
  if (!s.r1Ok) return {ROJO, "Falla sensor R1"};
  if (!s.r2Ok) return {ROJO, "Falla sensor R2"};
  if (s.llenado->falla != Falla::NINGUNA) return {ROJO, s.llenado->textoFalla()};
  if (s.transfer->falla != Falla::NINGUNA) return {ROJO, s.transfer->textoFalla()};
  if (s.r1 >= NIVEL_SEGURIDAD) return {ROJO, "R1: desborde"};
  if (s.r2 >= NIVEL_SEGURIDAD) return {ROJO, "R2: desborde"};
  if (s.r2 <= NIVEL_CRITICO) return {ROJO, "R2 casi vacio"};
  if (s.turbR2 == 2) return {ROJO, "R2 muy turbia"};
  if (s.goteo->mlRestantes() <= 0) return {ROJO, "Sin cloro"};
  if (s.turbR2 == 1) return {AMARILLO, "R2 turbia:filtro"};
  if (s.goteo->pctRestante() < GOTERO_AVISO_PCT) return {AMARILLO, "Recargar cloro"};
  if (s.goteo->dosisFueraDeRango()) return {AMARILLO, "Revisar goteo"};
  if (!s.goteo->contactoListo(s.ahora)) return {AMARILLO, "Clorando:espere"};
  if (!s.origen) return {AMARILLO, "Origen sin agua"};
  if (s.r2 <= R2_ENCENDER) return {AMARILLO, "R2 nivel bajo"};
  return {VERDE, "Agua lista"};
}
