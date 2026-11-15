package id.fir.robot;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Penyedia berkas sederhana (framework murni, tanpa AndroidX) untuk membuka
 * hasil unduhan SD lewat ACTION_VIEW. Hanya menyajikan berkas dari folder
 * Download khusus aplikasi (getExternalFilesDir(DIRECTORY_DOWNLOADS)) —
 * direktori yang sama tempat MainActivity menyimpan unduhan.
 *
 * URI bentuk: content://id.fir.robot.files/<nama berkas>
 */
public class FirFileProvider extends ContentProvider {

    static File downloadDir(android.content.Context ctx) {
        File d = ctx.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
        if (d == null) d = new File(ctx.getFilesDir(), "Download");
        if (!d.exists()) d.mkdirs();
        return d;
    }

    private File resolve(Uri uri) throws FileNotFoundException {
        String name = Uri.decode(uri.getLastPathSegment() == null
                ? "" : uri.getLastPathSegment());
        // Tolak segala bentuk penjelajahan path; hanya nama berkas polos.
        if (name.isEmpty() || name.contains("/") || name.contains("\\")
                || name.contains("..")) {
            throw new FileNotFoundException("Nama berkas tidak valid: " + uri);
        }
        File f = new File(downloadDir(getContext()), name);
        if (!f.isFile()) throw new FileNotFoundException("Tidak ditemukan: " + uri);
        return f;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode)
            throws FileNotFoundException {
        return ParcelFileDescriptor.open(resolve(uri),
                ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        String name = uri.getLastPathSegment() == null
                ? "" : uri.getLastPathSegment().toLowerCase();
        String ext = "";
        int dot = name.lastIndexOf('.');
        if (dot >= 0) ext = name.substring(dot + 1);
        String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
        return mime != null ? mime : "application/octet-stream";
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        return 0;
    }
}
