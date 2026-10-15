package org.beam.android;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

// A separate provider process in the test APK. Supply misleading size metadata
// so the service must stream/count actual bytes. Java avoids depending on the
// target APK's Kotlin runtime from the provider's independent class loader.
public final class FixtureProvider extends ContentProvider {
    public static final String CONTENT = "Provider snapshot 🌍\n世界\n";

    @Override
    public boolean onCreate() {
        return true;
    }
    @Override
    public String getType(Uri uri) {
        return "application/octet-stream";
    }
    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs,
                        String sortOrder) {
        MatrixCursor cursor =
            new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        cursor.addRow(new Object[] {"provider.txt", 1});
        return cursor;
    }
    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"/sample".equals(uri.getPath()) || !"r".equals(mode))
            throw new FileNotFoundException();
        File file = new File(getContext().getCacheDir(), "provider-fixture.txt");
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(CONTENT.getBytes(StandardCharsets.UTF_8));
        } catch (IOException error) {
            throw new FileNotFoundException(error.getMessage());
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }
    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
}
