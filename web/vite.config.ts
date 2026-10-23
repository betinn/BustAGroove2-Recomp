import { defineConfig, type Plugin } from 'vite';
import fs from 'node:fs';
import path from 'node:path';

// Dev-only: serve the disc image from BAG2_DISC (or web/.disc.json {"disc": "..."}) at /dev-disc.bin
// so the page can boot without the file picker. Nothing is copied into the repo.
function devDisc(): Plugin {
  return {
    name: 'bag2-dev-disc',
    configureServer(server) {
      let disc = process.env.BAG2_DISC;
      const cfg = path.resolve(__dirname, '.disc.json');
      if (!disc && fs.existsSync(cfg)) disc = JSON.parse(fs.readFileSync(cfg, 'utf8')).disc;
      server.middlewares.use('/dev-disc.bin', (req, res) => {
        if (!disc || !fs.existsSync(disc)) { res.statusCode = 404; res.end('BAG2_DISC not set'); return; }
        const size = fs.statSync(disc).size;
        const range = /bytes=(\d+)-(\d*)/.exec(req.headers.range ?? '');
        const start = range ? +range[1] : 0;
        const end = range && range[2] ? +range[2] : size - 1;
        res.statusCode = range ? 206 : 200;
        res.setHeader('Content-Type', 'application/octet-stream');
        res.setHeader('Accept-Ranges', 'bytes');
        res.setHeader('Content-Length', end - start + 1);
        if (range) res.setHeader('Content-Range', `bytes ${start}-${end}/${size}`);
        fs.createReadStream(disc, { start, end }).pipe(res);
      });
    },
  };
}

export default defineConfig({
  plugins: [devDisc()],
  server: {
    headers: {
      // COOP/COEP enable SharedArrayBuffer (EJS_threads). Local only.
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
    port: 5173,
    strictPort: true,
  },
});
