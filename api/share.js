// Vercel Serverless Function: Circuit Share & Export Resolver

export const config = {
  runtime: 'edge',
};

export default async function handler(req) {
  if (req.method === 'POST') {
    try {
      const { name, netlist, components } = await req.json();
      const payload = JSON.stringify({ name: name || 'Untitled', netlist, components, timestamp: Date.now() });
      const encoded = btoa(unescape(encodeURIComponent(payload)));
      
      return new Response(JSON.stringify({ id: encoded, url: `/?circuit=${encoded}` }), {
        status: 200,
        headers: { 'Content-Type': 'application/json' }
      });
    } catch (err) {
      return new Response(JSON.stringify({ error: err.message }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' }
      });
    }
  }

  if (req.method === 'GET') {
    const { searchParams } = new URL(req.url);
    const id = searchParams.get('id');
    if (!id) {
      return new Response(JSON.stringify({ error: 'Missing circuit id' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' }
      });
    }

    try {
      const decoded = decodeURIComponent(escape(atob(id)));
      const data = JSON.parse(decoded);
      return new Response(JSON.stringify(data), {
        status: 200,
        headers: { 'Content-Type': 'application/json' }
      });
    } catch (err) {
      return new Response(JSON.stringify({ error: 'Invalid circuit identifier' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json' }
      });
    }
  }

  return new Response(JSON.stringify({ error: 'Method not allowed' }), {
    status: 405,
    headers: { 'Content-Type': 'application/json' }
  });
}
