import json, math, time, http.server, os

D = os.path.dirname(os.path.abspath(__file__))
CICLO = 120  # segundos que tarda la demo en recorrer todas las etapas


def fase():
    t = time.time() % CICLO
    for fin, nombre in [(10, 'entrada'), (15, 'midiendo'), (20, 'dosificando'), (35, 'coagulando'),
                        (55, 'floculando'), (70, 'sedimentando'), (80, 'purga'), (95, 'transfer'),
                        (110, 'cloro'), (120, 'lista')]:
        if t < fin:
            return nombre, fin - t
    return 'lista', 0


def estado():
    f, falta = fase()
    falta = int(falta)
    coag_txt = {'entrada': 'Agua por tratar', 'midiendo': 'Midiendo R1', 'dosificando': 'Dosificando PAC',
                'coagulando': 'Coagulando', 'floculando': 'Floculando', 'sedimentando': 'Sedimentando'}.get(f, 'R1 tratada')
    ocupado = f in ('midiendo', 'dosificando', 'coagulando', 'floculando', 'sedimentando')
    rpm = 150 if f == 'coagulando' else 40 if f == 'floculando' else 0
    ntu1 = 85 if f in ('entrada', 'midiendo', 'dosificando', 'coagulando', 'floculando') else (40 if f == 'sedimentando' else 18)
    nivel1 = 72 if f not in ('transfer', 'cloro', 'lista') else 15
    nivel2 = 46 if f not in ('transfer', 'cloro', 'lista') else 88
    cloro_txt = {'transfer': 'Entra agua', 'cloro': 'Clorando:espere'}.get(f, 'Agua lista')
    causa = {'entrada': 'Entra agua cruda a R1', 'transfer': 'Pasando agua R1 → filtro → R2',
             'purga': 'Purgando lodo de R1', 'cloro': 'Clorando R2: espere 30 min',
             'lista': 'Agua segura lista en R2'}.get(f, 'R1: ' + coag_txt)
    return {
        "alerta": "VERDE" if f == 'lista' else "AMARILLO", "causa": causa, "uptime_s": 1234,
        "r1": {"agua": True, "ntu": ntu1, "sedimento": 2 if ntu1 > 50 else (1 if ntu1 > 20 else 0),
               "disponible": f in ('transfer', 'cloro', 'lista'), "motivo": coag_txt, "nivel_ok": True,
               "nivel": nivel1, "litros": round(nivel1 * 2 / 100, 1), "valvula": f == 'entrada',
               "valvula_motivo": "R1 en reposo" if f == 'entrada' else "R1 en tratamiento", "llenados": 3,
               "tds": 420, "tds_fuente": 410, "rango_tds": 0},
        "coag": {"estado": coag_txt, "pendiente": f == 'entrada', "ocupado": ocupado,
                 "falta_s": falta if ocupado else 0, "rpm": rpm, "pac_on": f == 'dosificando',
                 "ntu_medida": 85, "ppm": 30, "litros": 1.4, "ml_objetivo": 4.2,
                 "ml_aplicados": 2 if f == 'dosificando' else 4.2, "sin_pac": False, "sin_nivel": False,
                 "fuera_tabla": False, "lotes": 3, "ml_total": 13, "ml_restantes": 987, "pct_restante": 99},
        "purga": {"on": f == 'purga', "auto": True, "estado": "PULSO" if f == 'purga' else "LIMPIO", "pulso": 2,
                  "max_pulsos": 6, "motivo": "sacando lodo" if f == 'purga' else "sin sedimentos", "falla": False,
                  "purgas": 2, "seg": 4, "litros": 0.3, "ntu_antes": 160, "ntu_despues": 22},
        "transfer": {"on": f == 'transfer', "auto": True, "motivo": "R2 bajo" if f == 'transfer' else "R1 ocupado",
                     "falla": "", "ciclos": 2, "seg": 60, "litros": 4, "respuesta_s": 3.2, "completos": 2, "cortes": 0},
        "r2": {"ok": True, "nivel": nivel2, "litros": round(nivel2 * 2 / 100, 1), "ph": 7.2, "gotas_l": 1.0, "rango_ph": 1,
               "ntu": 0.8, "clara": True},
        "cloro": {"estado": cloro_txt, "goteo": False, "pendientes_l": 0, "objetivo": 2, "aplicadas": 2, "total": 4,
                  "ph_dosis": 7.1, "gotas_l_usadas": 1.0, "dosis": 3, "falta_s": falta * 15 if f == 'cloro' else 0,
                  "ml_restantes": 420, "pct_restante": 84, "gotas_ml": 20},
        "cfg": {"sed_medio": 50, "sed_limpio": 20, "ph_min": 6.5, "ph_opt": 7.5, "ph_max": 8.0, "gotas_l": 1.0,
                "turb_max": 5, "turb_ideal": 1, "r2_on": 20, "r2_off": 95, "coag_min": 10, "pac_stock": 10000,
                "rpm_rapida": 150, "rpm_lenta": 40, "modo_demo": True, "claves_defecto": True, "tds_bueno": 600,
                "tds_max": 1000, "tds_bloquea": False},
        "geom": {"r1": {"vacio": 25.0, "lleno": 6.0, "litros": 2.0, "dist": 11.3},
                 "r2": {"vacio": 25.0, "lleno": 6.0, "litros": 2.0, "dist": 16.3}},
        "nube": {"activa": True, "internet": True, "envios": 42, "errores": 1, "hace_s": int(time.time()) % 20, "codigo": 200, "canal": 3594272, "lectura": os.environ.get("TS_READ_KEY", "")}}


def hist():
    d = []
    for i in range(120):
        e = 24 if 40 < i < 60 else (1 if 62 < i < 68 else (2 if 70 < i < 90 else (4 if 92 < i < 96 else 0)))
        d.append([min(95, int(10 + i * 0.5)), int(150 - i) if i < 80 else 20, 70 + int(3 * math.sin(i / 9)), e,
                  max(1, int(10 * math.exp(-i / 30))), 420])
    return {"intervalo_s": 10, "max": 180, "datos": d}


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def j(self, o):
        b = json.dumps(o).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        p = self.path.split('?')[0]
        if p == '/api/estado':
            return self.j(estado())
        if p == '/api/historial':
            return self.j(hist())
        f = {'/': 'index.html', '/panel': 'panel.html', '/dash': 'dashboard.html'}.get(p)
        if not f:
            self.send_response(404)
            self.end_headers()
            return
        b = open(os.path.join(D, f), 'rb').read()  # se relee en cada pedido: editar y recargar
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length', 0)))
        self.j({"ok": True})


http.server.ThreadingHTTPServer(('127.0.0.1', 8181), H).serve_forever()
