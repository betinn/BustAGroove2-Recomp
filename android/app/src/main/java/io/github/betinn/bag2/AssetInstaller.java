package io.github.betinn.bag2;

import android.content.res.AssetManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Extrai assets/bag2/** para a pasta de arquivos do app. O manifesto (assets/bag2/manifest.txt, escrito por
 * recomp/android/stage.py) tem na primeira linha `version=<carimbo>` ou `mode=external`, e depois uma linha
 * por arquivo: `<caminho relativo>\t<tamanho em bytes>`. O carimbo instalado fica em
 * <files>/.bag2-assets-version; carimbo igual = nada a fazer. Arquivos de usuário (saves/, log.txt,
 * overlay_captures.json) nunca são tocados.
 */
public final class AssetInstaller {
    public static final String ASSET_ROOT = "bag2";
    private static final String STAMP = ".bag2-assets-version";

    public interface Progress { void update(long doneBytes, long totalBytes, String currentFile); }

    public static final class Manifest {
        public boolean external;
        public String version = "";
        public final List<String> paths = new ArrayList<>();
        public final List<Long> sizes = new ArrayList<>();
        public long total;
    }

    private final AssetManager mAssets;
    private final File mDest;

    public AssetInstaller(AssetManager assets, File dest) {
        mAssets = assets;
        mDest = dest;
    }

    public Manifest readManifest() throws IOException {
        Manifest m = new Manifest();
        try (InputStream in = mAssets.open(ASSET_ROOT + "/manifest.txt");
             BufferedReader r = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8))) {
            String line;
            boolean first = true;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                if (first) {
                    first = false;
                    if (line.startsWith("mode=external")) { m.external = true; continue; }
                    if (line.startsWith("version=")) { m.version = line.substring(8).trim(); continue; }
                }
                int tab = line.indexOf('\t');
                if (tab < 0) continue;
                m.paths.add(line.substring(0, tab));
                long size = Long.parseLong(line.substring(tab + 1).trim());
                m.sizes.add(size);
                m.total += size;
            }
        }
        return m;
    }

    public boolean isInstalled(Manifest m) throws IOException {
        File stamp = new File(mDest, STAMP);
        if (!stamp.isFile()) return false;
        String v;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                new java.io.FileInputStream(stamp), StandardCharsets.UTF_8))) {
            v = r.readLine();
        }
        return v != null && v.trim().equals(m.version) && new File(mDest, "game.toml").isFile();
    }

    public void install(Manifest m, Progress progress) throws IOException {
        if (!mDest.isDirectory() && !mDest.mkdirs()) throw new IOException("sem pasta " + mDest);
        new File(mDest, STAMP).delete();
        byte[] buf = new byte[1 << 20];
        long done = 0;
        for (int i = 0; i < m.paths.size(); i++) {
            String rel = m.paths.get(i);
            long size = m.sizes.get(i);
            File out = new File(mDest, rel);
            File dir = out.getParentFile();
            if (dir != null && !dir.isDirectory() && !dir.mkdirs()) throw new IOException("sem pasta " + dir);
            progress.update(done, m.total, rel);
            // Já extraído com o tamanho certo (instalação interrompida e retomada): pula.
            if (out.isFile() && out.length() == size) { done += size; continue; }
            File tmp = new File(dir, out.getName() + ".part");
            try (InputStream in = mAssets.open(ASSET_ROOT + "/" + rel, AssetManager.ACCESS_STREAMING);
                 FileOutputStream os = new FileOutputStream(tmp)) {
                int n;
                long fileDone = 0;
                while ((n = in.read(buf)) > 0) {
                    os.write(buf, 0, n);
                    fileDone += n;
                    if ((fileDone & ((16 << 20) - 1)) < (1 << 20)) progress.update(done + fileDone, m.total, rel);
                }
            }
            if (!tmp.renameTo(out)) {
                out.delete();
                if (!tmp.renameTo(out)) throw new IOException("não renomeou " + tmp);
            }
            done += size;
        }
        progress.update(m.total, m.total, "");
        try (FileWriter w = new FileWriter(new File(mDest, STAMP))) {
            w.write(m.version + "\n");
        }
    }
}
