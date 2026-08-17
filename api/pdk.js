// Vercel Serverless Function: PDK & Standard Cell Library Provider
// Serves open-source SkyWater 130nm / GF180MCU parameters and device models

export const config = {
  runtime: 'edge',
};

const PDK_CATALOG = {
  sky130: {
    name: 'SkyWater 130nm Open PDK',
    version: 'v0.0.1',
    voltage: '1.8V / 3.3V / 5.0V',
    devices: [
      { type: 'NMOS', model: 'sky130_fd_pr__nfet_01v8', vth0: 0.72, tox: 4.1e-9, u0: 670 },
      { type: 'PMOS', model: 'sky130_fd_pr__pfet_01v8', vth0: -0.84, tox: 4.2e-9, u0: 250 },
      { type: 'RES', model: 'sky130_fd_pr__res_high_po', rsq: 350.0 },
      { type: 'CAP', model: 'sky130_fd_pr__cap_mim_m3_1', c_per_um2: 2.0e-15 }
    ],
    standard_cells: ['sky130_fd_sc_hd__inv_1', 'sky130_fd_sc_hd__nand2_1', 'sky130_fd_sc_hd__nor2_1', 'sky130_fd_sc_hd__dfxtp_1']
  },
  gf180mcu: {
    name: 'GlobalFoundries 180nm MCU Open PDK',
    version: 'v0.0.2',
    voltage: '3.3V / 5.0V',
    devices: [
      { type: 'NMOS', model: 'nfet_03v3', vth0: 0.65, tox: 7.2e-9, u0: 550 },
      { type: 'PMOS', model: 'pfet_03v3', vth0: -0.70, tox: 7.3e-9, u0: 210 },
      { type: 'RES', model: 'r_ppoly', rsq: 310.0 }
    ],
    standard_cells: ['gf180mcu_fd_sc_mcu7t5v0__inv_1', 'gf180mcu_fd_sc_mcu7t5v0__nand2_1']
  }
};

export default async function handler(req) {
  const { searchParams } = new URL(req.url);
  const pdk = searchParams.get('pdk') || 'sky130';

  const data = PDK_CATALOG[pdk] || PDK_CATALOG.sky130;
  return new Response(JSON.stringify(data), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'public, s-maxage=86400, stale-while-revalidate=43200'
    }
  });
}
