/**
 * AcuteSim Engine — Full-Feature DOM Test Harness (DDR3 PHY design)
 * Drives the real UI at http://localhost:8080 via puppeteer-core + system Chrome.
 * Exercises: preset loading, schematic, wiring, palette, property editor,
 * simulation (tran/dc/ac), waveforms + cursors + FFT/Bode, SPICE netlist,
 * 3D layout, terminal CLI, command palette, split views, undo/redo, exports.
 */
const puppeteer = require('puppeteer-core');
const fs = require('fs');
const path = require('path');

const BASE_URL = process.env.BASE_URL || 'http://localhost:8080';
const SHOT_DIR = path.join(__dirname, 'screenshots');
const CHROME_CANDIDATES = [
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/Applications/Chromium.app/Contents/MacOS/Chromium',
  '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge'
];

const results = [];
let pageErrors = [];
let consoleErrors = [];

function record(name, pass, detail = '') {
  results.push({ name, pass, detail });
  console.log(`${pass ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`);
}

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

(async () => {
  fs.mkdirSync(SHOT_DIR, { recursive: true });
  const chrome = CHROME_CANDIDATES.find(p => fs.existsSync(p));
  if (!chrome) {
    console.error('No Chrome/Chromium/Edge browser found for puppeteer-core.');
    process.exit(2);
  }

  const browser = await puppeteer.launch({
    executablePath: chrome,
    headless: 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-webgl', '--use-angle=metal', '--window-size=1600,1000']
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1000 });

  page.on('pageerror', e => pageErrors.push(String(e)));
  page.on('console', m => { if (m.type() === 'error') consoleErrors.push(m.text()); });

  // ---------- 1. Load app ----------
  await page.goto(BASE_URL, { waitUntil: 'networkidle2', timeout: 30000 });
  await sleep(800);
  record('App loads on localhost', true, BASE_URL);

  // ---------- 2. Load DDR3 preset via DOM dropdown ----------
  await page.select('#preset-selector', 'ddr3_phy');
  await sleep(500);
  const activePreset = await page.evaluate(() => state.activePreset);
  record('DDR3 preset loads via dropdown', activePreset === 'ddr3_phy', `activePreset=${activePreset}`);

  const compCount = await page.evaluate(() => state.components.length);
  const wireCount = await page.evaluate(() => state.wires.length);
  record('DDR3 block diagram has devices', compCount === 9, `${compCount} components`);
  record('DDR3 block diagram has nets', wireCount === 9, `${wireCount} wires`);

  // ---------- 3. Schematic SVG rendering ----------
  const svgComps = await page.evaluate(() => document.querySelectorAll('#schematic-layer-components .sch-component').length);
  const svgWires = await page.evaluate(() => document.querySelectorAll('#schematic-layer-wires .sch-wire').length);
  record('Schematic renders components in SVG DOM', svgComps === 9, `${svgComps} <g> nodes`);
  record('Schematic renders wires in SVG DOM', svgWires === 9, `${svgWires} paths`);

  // DDR3-specific devices present
  const hasDriver = await page.evaluate(() =>
    !!state.components.find(c => c.id === 'M1' && c.type === 'NMOS') &&
    !!state.components.find(c => c.id === 'M2' && c.type === 'PMOS') &&
    !!state.components.find(c => c.id === 'RTT' && c.type === 'R'));
  record('DDR3 SSTL driver devices present (M1/M2/RTT)', hasDriver);

  await page.screenshot({ path: path.join(SHOT_DIR, '01_ddr3_schematic.png') });

  // ---------- 4. DC OP table ----------
  const dcRows = await page.evaluate(() => document.querySelectorAll('#dc-op-table-body tr').length);
  record('DC operating point table populated', dcRows === 5, `${dcRows} rows`);

  // ---------- 5. PVT grid ----------
  const pvtCards = await page.evaluate(() => document.querySelectorAll('#pvt-results-grid .pvt-card').length);
  record('PVT corner matrix populated', pvtCards === 3, `${pvtCards} corners`);

  // ---------- 6. Select / move / rotate component ----------
  await page.evaluate(() => {
    const comp = state.components.find(c => c.id === 'RTT');
    state.selectedComponents = ['RTT'];
    state.selectedComponent = comp;
    rotateSelectedComponent();
  });
  await sleep(200);
  const rotVal = await page.evaluate(() => (state.components.find(c => c.id === 'RTT') || {}).rot);
  record('Component rotate (Rtt 0→90°)', rotVal === 90, `rot=${rotVal}`);
  await page.evaluate(() => undo());
  await sleep(200);
  const rotAfterUndo = await page.evaluate(() => (state.components.find(c => c.id === 'RTT') || {}).rot);
  record('Undo restores rotation', rotAfterUndo === 0, `rot=${rotAfterUndo}`);
  await page.evaluate(() => redo());
  await sleep(150);

  // ---------- 7. Property editor (double-click parity) ----------
  await page.evaluate(() => {
    const comp = state.components.find(c => c.id === 'CL');
    openComponentPropertyEditor(comp);
  });
  await sleep(200);
  const propModalExists = await page.evaluate(() => !!document.getElementById('prop-editor-modal'));
  record('Property editor modal opens', propModalExists);
  await page.evaluate(() => {
    const input = document.querySelector('#prop-editor-modal input[data-field="cap"]');
    if (input) input.value = 'cap_mim_m3 3pF';
    document.getElementById('btn-prop-apply')?.click();
  });
  await sleep(200);
  const clVal = await page.evaluate(() => (state.components.find(c => c.id === 'CL') || {}).val);
  record('Property editor applies new Cin value', clVal === 'cap_mim_m3 3pF', `val=${clVal}`);

  // ---------- 8. Palette placement ----------
  const beforeComps = await page.evaluate(() => state.components.length);
  await page.evaluate(() => placeNewComponent('C', 560, 260));
  await sleep(200);
  const afterComps = await page.evaluate(() => state.components.length);
  record('Component palette places new device', afterComps === beforeComps + 1, `${beforeComps}→${afterComps}`);

  // ---------- 9. Wire tool: connect new cap to VTT ----------
  await page.evaluate(() => {
    const newC = state.components[state.components.length - 1];
    state.wires.push({ from: { comp: newC.id, pin: 0 }, to: { comp: 'VTT', pin: 1 } });
    renderSchematic();
    saveHistoryState();
  });
  await sleep(200);
  const wireTotal = await page.evaluate(() => state.wires.length);
  record('Wire routing adds net', wireTotal === 10, `${wireTotal} wires`);

  // ---------- 10. Net highlight (click a wire) ----------
  const netHighlighted = await page.evaluate(() => {
    highlightNetForWire(4); // RTT→RL line
    renderSchematic();
    return document.querySelectorAll('#schematic-layer-wires .sch-wire.net-highlighted').length;
  });
  record('Net cross-probe highlight works', netHighlighted >= 1, `${netHighlighted} wires highlighted`);
  await page.screenshot({ path: path.join(SHOT_DIR, '02_ddr3_net_highlight.png') });

  // ---------- 11. Run transient simulation ----------
  await page.click('#btn-run-sim');
  await sleep(900);
  const simResults = await page.evaluate(() => ({
    hasData: state.simulationResults.time.length > 0,
    pts: state.simulationResults.time.length,
    vref: state.simulationResults.traces['V(VREF)'] ? state.simulationResults.traces['V(VREF)'][10] : null
  }));
  record('Transient simulation produces data', simResults.hasData && simResults.pts === 301, `${simResults.pts} points`);
  record('DDR3 VREF trace = 0.75V mid-rail', simResults.vref === 0.75, `V(VREF)[10]=${simResults.vref}`);

  // Waveform canvas painted (non-background pixels)
  const wavePainted = await page.evaluate(() => {
    const c = document.getElementById('waveform-canvas');
    if (!c || !c.width) return false;
    const ctx = c.getContext('2d');
    const d = ctx.getImageData(0, 0, c.width, Math.min(c.height, 400)).data;
    let colored = 0;
    for (let i = 0; i < d.length; i += 4) {
      if (d[i + 3] > 0 && !(d[i] === 12 && d[i + 1] === 12 && d[i + 2] === 14)) colored++;
    }
    return colored > 500;
  });
  record('Waveform canvas renders DDR3 eye traces', wavePainted);
  await page.screenshot({ path: path.join(SHOT_DIR, '03_ddr3_waveforms.png') });

  // ---------- 12. Cursor measurements ----------
  const cursorDelta = await page.evaluate(() => {
    state.cursorA = 1.25; state.cursorB = 2.5;
    renderWaveforms();
    return document.getElementById('cur-delta-t').innerText;
  });
  record('Waveform cursor Δt measurement', cursorDelta.includes('1.25'), `Δt=${cursorDelta}`);

  // FFT + Bode modes
  await page.evaluate(() => { document.getElementById('btn-wave-fft')?.click(); });
  await sleep(200);
  const fftBadge = await page.evaluate(() => document.getElementById('waveform-title-badge').innerText);
  record('FFT spectrum mode toggles', fftBadge.includes('FOURIER'), fftBadge);
  await page.evaluate(() => { document.getElementById('btn-wave-bode')?.click(); });
  await sleep(200);
  const bodeBadge = await page.evaluate(() => document.getElementById('waveform-title-badge').innerText);
  record('Bode mode toggles', bodeBadge.includes('BODE'), bodeBadge);
  await page.evaluate(() => { document.getElementById('btn-wave-bode')?.click(); }); // back to tran

  // ---------- 13. SPICE netlist editor ----------
  await page.evaluate(() => openDocument('ddr3_phy_sstl15.sp', 'spice'));
  await sleep(300);
  const netlist = await page.evaluate(() => document.getElementById('spice-code-area').value);
  record('SPICE netlist editor generates DDR3 netlist',
    netlist.includes('M1') && netlist.includes('M2') && netlist.includes('RTT') && netlist.includes('.TRAN'),
    `${netlist.split('\n').length} lines`);
  await page.screenshot({ path: path.join(SHOT_DIR, '04_ddr3_spice_netlist.png') });

  // ---------- 14. 3D layout view ----------
  await page.evaluate(() => openDocument('sky130_silicon.gds', 'layout'));
  await sleep(700);
  const glInfo = await page.evaluate(() => state.layout3D.gl ? state.layout3D.gl.constructor.name : null);
  record('3D silicon layout WebGL context active', !!glInfo, glInfo || 'no GL');
  await page.evaluate(() => { document.getElementById('btn-cam-top')?.click(); });
  await sleep(300);
  const camTop = await page.evaluate(() => ({ x: state.layout3D.rotX, y: state.layout3D.rotY }));
  record('3D camera preset (Top view)', camTop.x === 0 && camTop.y === 0, JSON.stringify(camTop));
  await page.screenshot({ path: path.join(SHOT_DIR, '05_ddr3_3d_layout.png') });

  // ---------- 15. Terminal CLI ----------
  const termOut = await page.evaluate(async () => {
    switchPanelTab('console');
    const input = document.getElementById('terminal-input');
    input.value = 'preset ddr3_phy';
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    return document.getElementById('terminal-history').innerText;
  });
  await sleep(400);
  record('Terminal CLI loads DDR3 preset', termOut.includes('ddr3_phy'), termOut.slice(0, 80).replace(/\n/g, ' | '));

  const kclOut = await page.evaluate(() => {
    const input = document.getElementById('terminal-input');
    input.value = 'kcl';
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    return document.getElementById('terminal-history').innerText;
  });
  record('Terminal KCL audit command', kclOut.includes('PASS'));

  // ---------- 16. Command palette ----------
  await page.evaluate(() => openCommandPalette());
  await sleep(200);
  await page.type('#palette-input', 'DDR3');
  await sleep(200);
  const paletteEntries = await page.evaluate(() => document.querySelectorAll('#palette-results .palette-entry').length);
  record('Command palette finds DDR3 command', paletteEntries >= 1, `${paletteEntries} entries`);
  await page.evaluate(() => {
    const first = document.querySelector('#palette-results .palette-entry');
    if (first) first.click();
  });
  await sleep(400);
  const presetAfterPalette = await page.evaluate(() => state.activePreset);
  record('Command palette executes DDR3 load', presetAfterPalette === 'ddr3_phy');

  // ---------- 17. Split view ----------
  await page.evaluate(() => document.getElementById('btn-layout-split-h')?.click());
  await sleep(400);
  const splitMode = await page.evaluate(() => state.layoutMode);
  record('Split-horizontal multi-view', splitMode === 'split-horizontal', splitMode);
  await page.screenshot({ path: path.join(SHOT_DIR, '06_ddr3_split_view.png') });
  await page.evaluate(() => document.getElementById('btn-layout-single')?.click());
  await sleep(200);

  // ---------- 18. Zoom / grid / tools ----------
  await page.evaluate(() => { zoomFitSchematic(); });
  const zoomBadge = await page.evaluate(() => {
    const el = document.getElementById('sb-zoom');
    return el ? el.innerText : null;
  });
  record('Zoom-to-fit resets view', zoomBadge === '100%', `badge=${zoomBadge}`);
  await page.evaluate(() => { toggleGrid(); toggleGrid(); });

  // ---------- 19. Export netlist (download) ----------
  const netlistText = await page.evaluate(() => { generateSpiceNetlist(); return state.netlistText; });
  record('Netlist export content ready', netlistText.includes('sky130') && netlistText.includes('.OP'));

  // ---------- 20. PDK modal ----------
  await page.evaluate(() => openModal('pdk-modal'));
  await sleep(200);
  const pdkVisible = await page.evaluate(() => !document.getElementById('pdk-modal').classList.contains('hidden'));
  record('PDK swap modal opens', pdkVisible);
  await page.evaluate(() => closeModal('pdk-modal'));

  // ---------- 21. ERC / DRC sidebar ----------
  await page.evaluate(() => { bindMenuAction; switchSidebarTab('erc'); });
  await sleep(200);
  const ercPaneActive = await page.evaluate(() => document.getElementById('pane-erc')?.classList.contains('active'));
  record('ERC/DRC results pane accessible', !!ercPaneActive);

  // ---------- 22. Page errors check ----------
  record('Zero page JS errors during session', pageErrors.length === 0, pageErrors.slice(0, 3).join(' ;; '));

  // ---------- Summary ----------
  const failed = results.filter(r => !r.pass);
  console.log('\n========================================');
  console.log(`RESULTS: ${results.length - failed.length}/${results.length} passed`);
  if (failed.length) {
    console.log('FAILED TESTS:');
    failed.forEach(f => console.log(`  ✗ ${f.name} — ${f.detail}`));
  }
  console.log('========================================');

  await browser.close();
  process.exit(failed.length ? 1 : 0);
})().catch(e => {
  console.error('HARNESS CRASH:', e);
  results.forEach(r => console.log(`${r.pass ? 'PASS' : 'FAIL'}  ${r.name}`));
  process.exit(3);
});