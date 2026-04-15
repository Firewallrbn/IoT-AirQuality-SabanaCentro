// =========================================================
// RELOJ EN VIVO
// =========================================================
function tickReloj() {
  const el = document.getElementById('reloj');
  if (el) el.textContent =
    new Date().toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}
tickReloj();
setInterval(tickReloj, 1000);
 
// =========================================================
// 1. CONFIGURACIÓN CHART.JS
// =========================================================
const timeLabels = Array.from({ length: 30 }, (_, i) => {
  const sec = (29 - i) * 2;
  return sec === 0 ? 'Ahora' : `-${sec}s`;
});
 
Chart.defaults.color = '#5a7a9e';
Chart.defaults.font.family = "'IBM Plex Mono', 'Courier New', monospace";
Chart.defaults.font.size = 11;
 
function makeOptions(yLabel) {
  return {
    responsive: true,
    maintainAspectRatio: false,
    animation: { duration: 300 },
    plugins: {
      legend: { display: false },
      tooltip: {
        mode: 'index',
        intersect: false,
        backgroundColor: 'rgba(8,14,26,0.96)',
        titleColor: '#e2eaf6',
        bodyColor: '#94a3b8',
        borderColor: '#1e3050',
        borderWidth: 1,
        padding: 10,
        callbacks: {
          label: ctx => {
            const v = ctx.parsed.y;
            return v != null ? ` ${v.toFixed(1)} ${yLabel}` : ' —';
          }
        }
      }
    },
    scales: {
      x: {
        grid: { display: false },
        border: { display: false },
        ticks: { maxTicksLimit: 7, maxRotation: 0 }
      },
      y: {
        beginAtZero: true,
        grid: { color: 'rgba(30,48,80,0.6)', drawBorder: false },
        border: { dash: [3, 3], display: false },
        ticks: { maxTicksLimit: 5 }
      }
    },
    elements: {
      point: { radius: 0, hitRadius: 12, hoverRadius: 5 },
      line:  { tension: 0.4, borderWidth: 2.5 }
    },
    interaction: { mode: 'nearest', axis: 'x', intersect: false }
  };
}
 
const ctxPM = document.getElementById('chartPM').getContext('2d');
const gradPM = ctxPM.createLinearGradient(0, 0, 0, 200);
gradPM.addColorStop(0, 'rgba(59,130,246,0.28)');
gradPM.addColorStop(1, 'rgba(59,130,246,0)');
 
const chartPM = new Chart(ctxPM, {
  type: 'line',
  data: {
    labels: timeLabels,
    datasets: [{
      label: 'PM 2.5',
      data: Array(30).fill(null),
      borderColor: '#3b82f6',
      backgroundColor: gradPM,
      fill: true
    }]
  },
  options: makeOptions('µg/m³')
});
 
const ctxGas = document.getElementById('chartGas').getContext('2d');
const gradGas = ctxGas.createLinearGradient(0, 0, 0, 200);
gradGas.addColorStop(0, 'rgba(239,68,68,0.22)');
gradGas.addColorStop(1, 'rgba(239,68,68,0)');
 
const chartGas = new Chart(ctxGas, {
  type: 'line',
  data: {
    labels: timeLabels,
    datasets: [{
      label: 'VOC',
      data: Array(30).fill(null),
      borderColor: '#ef4444',
      backgroundColor: gradGas,
      fill: true
    }]
  },
  options: makeOptions('ppm')
});
 
// =========================================================
// 2. CALIDAD — SINCRONIZADA CON UMBRALES DEL .ino
// =========================================================
// ESP32 usa: LIM_PM_MODERADO = 37.0 | LIM_VOC_CRITICO = 800.0
// REGLA: pm <= 37 → BUENA | pm > 37 → POLVO REGIONAL (como mínimo)
// Estados posibles: BUENA · POLVO REGIONAL · POLUCION MIXTA
//                   INV. TERMICA! · FOCO CERCANO · NIEBLA PURA
 
// PM2.5: el .ino solo tiene DOS zonas definidas por umbrales propios:
//   pm <= LIM_PM_MODERADO (37) → estado base BUENA
//   pm >  LIM_PM_MODERADO (37) → puede derivar en POLVO REGIONAL o POLUCION MIXTA
// Las categorías extra (insalubre, muy insalubre) se mantienen para la barra AQI visual
// pero el badge sigue SIEMPRE al campo "estado" que viene del ESP32.
 
function getCalidad(pm) {
  // Umbral exacto del firmware: LIM_PM_MODERADO = 37.0
  if (pm <= 37)  return { texto: 'Buena',         color: '#10b981', pct: Math.max(1, pm / 37 * 35) };
  if (pm <= 55)  return { texto: 'Polvo regional', color: '#f97316', pct: 35 + (pm - 37) / 18 * 20 };
  if (pm <= 150) return { texto: 'Insalubre',      color: '#ef4444', pct: 55 + (pm - 55) / 95 * 25 };
  if (pm <= 250) return { texto: 'Muy insalubre',  color: '#a855f7', pct: 80 + (pm - 150) / 100 * 15 };
  return           { texto: 'Peligroso',           color: '#dc2626', pct: 98 };
}
 
// VOC: LIM_VOC_CRITICO = 800 | segundo umbral = 1200 (FOCO CERCANO)
function getCalidadGas(ppm) {
  if (ppm <= 800)  return { texto: 'Normal',     color: '#10b981' };
  if (ppm <= 1200) return { texto: 'Crítico',    color: '#ef4444' };
  return             { texto: 'Foco cercano',    color: '#dc2626' };
}
 
function calidadDotPM(pm) {
  const c = getCalidad(pm);
  return `<span style="display:inline-block;width:7px;height:7px;border-radius:50%;
    background:${c.color};margin-right:5px;vertical-align:middle;"></span>${c.texto}`;
}
 
function actualizarAQI(pm) {
  const c = getCalidad(pm);
  const fill  = document.getElementById('aqi-fill');
  const label = document.getElementById('aqi-label-text');
  if (fill)  { fill.style.width = c.pct + '%'; fill.style.background = c.color; }
  if (label) { label.textContent = `${pm.toFixed(1)} µg/m³ — ${c.texto}`; label.style.color = c.color; }
}
 
// =========================================================
// 3. TABLA HISTÓRICA + BITÁCORA
// =========================================================
let estadoAnterior = 'BUENA';
 
function actualizarTablaYBitacora(data) {
 
  // ---------- TABLA ----------
  const tbody = document.getElementById('history-body');
  if (!tbody) { console.warn('[AirMon] No se encontró #history-body'); return; }
 
  const hpm  = data.historial_pm;
  const hgas = data.historial_gas;
 
  if (!Array.isArray(hpm) || !Array.isArray(hgas) || hpm.length === 0) {
    tbody.innerHTML = `<tr><td colspan="4" style="text-align:center;padding:20px;
      color:#3d5c7e;font-size:0.78rem;">Esperando datos del sensor…</td></tr>`;
    return;
  }
 
  const len = hpm.length;
  let html = '';
 
  // ESP32: hist[0]=antiguo · hist[29]=Ahora
  // Tabla:  fila 0 = Ahora → idx = len-1
  for (let i = 0; i < len; i++) {
    const sec     = i * 2;
    const timeStr = sec === 0 ? 'Ahora' : `Hace ${sec}s`;
    const idx     = len - 1 - i;
    const pm      = hpm[idx];
    const gas     = hgas[idx];
    const pmOk    = typeof pm  === 'number' && isFinite(pm);
    const gasOk   = typeof gas === 'number' && isFinite(gas);
    const pmStr   = pmOk  ? pm.toFixed(1)   : '—';
    const gasStr  = gasOk ? Math.round(gas) : '—';
 
    // Columna calidad: PM + aviso VOC si supera umbral crítico (800 ppm = LIM_VOC_CRITICO)
    let calHtml = pmOk ? calidadDotPM(pm) : '—';
    if (gasOk && gas > 800) {
      const cg = getCalidadGas(gas);
      calHtml += `<br><span style="font-size:0.7rem;color:${cg.color};">VOC: ${cg.texto}</span>`;
    }
 
    html += `
<tr>
<td>${timeStr}</td>
<td class="val-pm">${pmStr}</td>
<td class="val-gas">${gasStr}</td>
<td style="font-size:0.75rem;line-height:1.6;">${calHtml}</td>
</tr>`;
  }
  tbody.innerHTML = html;
 
  // ---------- BITÁCORA ----------
  const estadoActual = data.estado || 'BUENA';
 
  if (estadoActual !== estadoAnterior &&
      estadoActual !== 'BUENA' &&
      !estadoActual.includes('Iniciando')) {
 
    const logBox = document.getElementById('alarm-log');
    if (logBox) {
      const vacioEl = logBox.querySelector('.log-empty');
      if (vacioEl) vacioEl.remove();
 
      const hora = new Date().toLocaleTimeString('es-CO');
 
      // Gravedad idéntica al motor de reglas del .ino
      let clase = 'alerta';
      const su = estadoActual.toUpperCase();
      if (su.includes('TERMICA') || su.includes('FOCO') ||
          su.includes('ROJA')    || su.includes('PELIGRO')) {
        clase = 'peligro';
      }
 
      // Iconos por estado
      const iconos = {
        'INV. TERMICA!':  '🔴',
        'FOCO CERCANO':   '🔴',
        'POLUCION MIXTA': '🟠',
        'POLVO REGIONAL': '🟡',
        'NIEBLA PURA':    '🔵',
      };
      const icono = iconos[estadoActual] || '⚠️';
 
      const item = document.createElement('div');
      item.className = `log-item ${clase}`;
      item.innerHTML = `
<span class="log-time">${hora}</span>
<span class="log-estado">${icono} ${estadoActual}</span>
<span class="log-vals">PM2.5: ${data.pm25.toFixed(1)} µg/m³ &nbsp;|&nbsp; VOC: ${Math.round(data.gas)} ppm</span>
      `;
      logBox.insertAdjacentElement('afterbegin', item);
    }
  }
  estadoAnterior = estadoActual;
}
 
// =========================================================
// 4. INDICADOR DE CONEXIÓN
// =========================================================
let fallosConsecutivos = 0;
 
function setConexion(ok) {
  const dot = document.querySelector('.dot-live');
  if (!dot) return;
  if (ok) {
    dot.style.background = '#10b981';
    fallosConsecutivos = 0;
  } else {
    fallosConsecutivos++;
    dot.style.background = fallosConsecutivos >= 2 ? '#ef4444' : '#f59e0b';
  }
}
 
// =========================================================
// 5. FUNCIÓN PRINCIPAL: FETCH + PINTAR UI
// =========================================================
async function actualizarDatos() {
  try {
    const response = await fetch('/datos');
 
    if (!response.ok) {
      console.warn('[AirMon] HTTP:', response.status);
      setConexion(false);
      return;
    }
 
    const data = await response.json();
    setConexion(true);
 
    const set = (id, val) => {
      const el = document.getElementById(id);
      if (el) el.textContent = val;
    };
 
    // Tarjetas
    set('val-pm',        data.pm25.toFixed(1));
    set('val-gas',       Math.round(data.gas));
    set('val-temp',      data.temp.toFixed(1));
    set('val-hum',       data.hum.toFixed(1));
    set('val-pres',      Math.round(data.pres));
    set('chart-pm-val',  data.pm25.toFixed(1) + ' µg/m³');
    set('chart-gas-val', Math.round(data.gas) + ' ppm');
 
    // Badge — refleja exactamente el motor de reglas del .ino
    // El campo data.estado viene directamente del ESP32 (estadoOLED),
    // por lo que el badge SIEMPRE muestra lo mismo que la pantalla física.
    const badge = document.getElementById('indicador-estado');
    if (badge) {
      badge.textContent = data.estado;
      badge.className   = 'badge';
      const su = (data.estado || '').toUpperCase();
 
      if (su.includes('TERMICA') || su.includes('FOCO') || su.includes('PELIGRO')) {
        badge.classList.add('peligro');          // ROJO  — INV. TERMICA! / FOCO CERCANO
      } else if (su.includes('MIXTA')) {
        badge.classList.add('alerta');           // NARANJA — POLUCION MIXTA
      } else if (su.includes('POLVO') || su.includes('REGIONAL')) {
        badge.classList.add('alerta');           // AMARILLO — POLVO REGIONAL
      } else if (su.includes('NIEBLA')) {
        badge.classList.add('info');             // AZUL — NIEBLA PURA
      } else {
        badge.classList.add('buena');            // VERDE — BUENA
      }
    }
 
    // Gráficas
    chartPM.data.datasets[0].data  = data.historial_pm;
    chartGas.data.datasets[0].data = data.historial_gas;
    chartPM.update('none');
    chartGas.update('none');
 
    // Barra AQI
    actualizarAQI(data.pm25);
 
    // Tabla + Bitácora
    actualizarTablaYBitacora(data);
 
    // Botón mute
    const btnMute = document.getElementById('btn-mute');
    const txtMute = document.getElementById('mute-status');
    if (btnMute && txtMute) {
      if (data.alarma_mute) {
        btnMute.textContent = 'Activar Alarma';
        btnMute.className   = 'btn muted';
        txtMute.textContent = 'El zumbador físico está silenciado por el operador.';
      } else {
        btnMute.textContent = 'Silenciar Alarma';
        btnMute.className   = 'btn active';
        txtMute.textContent = 'El sistema sonoro automático está operando con normalidad.';
      }
    }
 
  } catch (err) {
    console.error('[AirMon] Error:', err.message);
    setConexion(false);
  }
}
 
// =========================================================
// 6. BOTÓN MUTE
// =========================================================
async function toggleMute() {
  const btn = document.getElementById('btn-mute');
  try {
    if (btn) btn.textContent = 'Enviando…';
    await fetch('/mute');
    await actualizarDatos();
  } catch (err) {
    alert('Error de red al enviar el comando.');
    console.error(err);
  }
}
 
// =========================================================
// ARRANQUE
// =========================================================
actualizarDatos();
setInterval(actualizarDatos, 2000);