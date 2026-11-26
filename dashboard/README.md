# Dashboard (prototipo)

Prototipo de la interfaz web de AquaAlerta, fuera del firmware: todavía no está en `include/pagina.h`.

```bash
cd dashboard
python3 mock.py          # http://localhost:8181/dash
```

`mock.py` sirve las páginas y simula la API del ESP32 (`/api/estado`, `/api/historial`) con recipientes de 2 L,
igual que la maqueta. Para ver la vista de ThingSpeak, exportar la Read API Key del canal:
`TS_READ_KEY=<clave> python3 mock.py`.

- `dashboard.html`: panel principal (paleta clara) con la vista **Costo por familia** (`#simulador`):
  animación de un lote sobre la maqueta en cascada y la vivienda, y estimación del costo para una familia
  (equipo, operación mensual y recuperación frente al botellón). Precios referenciales, no cotizaciones.
- `dashboard.oscuro.html`: versión con paleta oscura.
- `index.html`, `panel.html`: página simple y panel de la red de aljibes.
