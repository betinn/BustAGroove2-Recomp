package io.github.betinn.bag2;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;

/**
 * Tela de preparação. O APK completo leva os dados do jogo em assets/bag2 (disco, BIOS, mods do remake,
 * texturas HD, game.toml...). O runtime lê arquivos comuns, então na primeira execução (ou quando o
 * carimbo do manifesto muda) tudo é extraído para getExternalFilesDir(null) =
 * /sdcard/Android/data/io.github.betinn.bag2/files, com barra de progresso; depois abre a BAG2Activity.
 *
 * Build de desenvolvimento (manifesto com mode=external): nada é extraído; os dados são enviados por
 * `adb push` para a mesma pasta e só se confere que game.toml existe.
 */
public class SetupActivity extends Activity {
    private final Handler mUi = new Handler(Looper.getMainLooper());
    private ProgressBar mProgress;
    private TextView mMessage, mDetail;
    private Thread mWorker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_setup);
        mProgress = findViewById(R.id.setup_progress);
        mMessage = findViewById(R.id.setup_message);
        mDetail = findViewById(R.id.setup_detail);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mWorker != null) return;
        final File files = getExternalFilesDir(null);
        final AssetInstaller installer = new AssetInstaller(getAssets(), files);
        mWorker = new Thread(() -> {
            try {
                AssetInstaller.Manifest m = installer.readManifest();
                if (m.external) {
                    if (!new File(files, "game.toml").isFile()) {
                        mUi.post(() -> {
                            mProgress.setIndeterminate(false);
                            mProgress.setProgress(0);
                            mMessage.setText(R.string.setup_external_missing);
                            mWorker = null;
                        });
                        return;
                    }
                } else if (!installer.isInstalled(m)) {
                    installer.install(m, (done, total, name) -> mUi.post(() -> {
                        mProgress.setIndeterminate(false);
                        mProgress.setProgress(total > 0 ? (int) (done * 1000 / total) : 0);
                        mDetail.setText(String.format("%d / %d MB  %s", done >> 20, total >> 20, name));
                    }));
                }
                mUi.post(this::launchGame);
            } catch (Exception e) {
                mUi.post(() -> {
                    mMessage.setText(getString(R.string.setup_error, e.toString()));
                    mWorker = null;
                });
            }
        }, "bag2-setup");
        mWorker.start();
    }

    private void launchGame() {
        Intent i = new Intent(this, BAG2Activity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(i);
        finish();
    }
}
