package org.labelqt.mobile;

import android.app.Activity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.system.Os;
import android.system.OsConstants;

import androidx.core.content.FileProvider;
import androidx.core.content.IntentCompat;

import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.ref.WeakReference;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;

/**
 * Platform transport only; archive/TXT validation and workspace publication belong to C++ services.
 *
 * Packaging contract (owned by the Android build integration):
 * - Activity: org.labelqt.mobile.LabelQtActivity, singleTop, narrow archive/TXT VIEW/SEND filters.
 * - Provider: org.labelqt.mobile.LabelQtActivity$ShareProvider, authority ${applicationId}.files,
 *   exported=false, grantUriPermissions=true, android.support.FILE_PROVIDER_PATHS=@xml/file_paths.
 * - Gradle: implementation("androidx.core:core:<toolchain-compatible pinned version>").
 *   Enable AndroidX and record its Apache-2.0 license in dependency notices.
 * - No storage-wide permissions. Preserve Qt's Activity metadata/configChanges when integrating.
 */
public class LabelQtActivity extends QtActivity {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final int PICK_FILE = 0x4c51;
    private static final String SAVED_HANDLED = "labelqt.bridge.intentHandled";
    private static final String SAVED_PICKER = "labelqt.bridge.pickerOpen";
    private static final int MAX_PENDING = 3;
    private static final long MAX_BYTES = 2L * 1024 * 1024 * 1024;
    private static final Pattern STAGED_NAME = Pattern.compile(
            "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.(zip|7z|rar|tar|gz|tgz|txt|part)");
    private static final String[][] INPUT_TYPES = {
            {"zip", "application/zip"}, {"zip", "application/x-zip-compressed"},
            {"zip", "application/x-zip"}, {"7z", "application/x-7z-compressed"},
            {"rar", "application/vnd.rar"}, {"rar", "application/x-rar-compressed"},
            {"rar", "application/x-rar"}, {"tar", "application/x-tar"},
            {"gz", "application/gzip"}, {"gz", "application/x-gzip"},
            {"tgz", "application/x-compressed-tar"}, {"txt", "text/plain"}
    };

    private static String[] inputMimeTypes() {
        String[] result = new String[INPUT_TYPES.length + 1];
        for (int i = 0; i < INPUT_TYPES.length; ++i)
            result[i] = INPUT_TYPES[i][1];
        // Some providers only identify archives as generic binary data. C++ validates content.
        result[INPUT_TYPES.length] = "application/octet-stream";
        return result;
    }

    // Keep in sync with AndroidFileBridge.cpp. Never send provider messages/URIs to UI or logs.
    private static final int UNAVAILABLE = 1;
    private static final int BUSY = 2;
    private static final int INVALID_INPUT = 3;
    private static final int TOO_LARGE = 4;
    private static final int PERMISSION_DENIED = 5;
    private static final int COPY_FAILED = 6;
    private static final int PICKER_FAILED = 7;
    private static final int SHARE_FAILED = 8;
    private static final int UNSUPPORTED_TYPE = 9;
    private static final int INTERRUPTED = 10;

    // Main-looper-owned, process-scoped transport; neither workers nor native code own an Activity.
    private static volatile BridgeState state;
    private boolean resumed;
    private boolean intentHandled;
    private boolean pickerOpen;

    private static native void nativeResult(long receiver, long eventId, String path, String displayName, int error);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        // Qt can start C++ inside super.onCreate; establish the cold-start mailbox first.
        if (state == null)
            state = new BridgeState(getApplicationContext());
        state.activity = new WeakReference<>(this);
        intentHandled = savedInstanceState != null && savedInstanceState.getBoolean(SAVED_HANDLED);
        pickerOpen = savedInstanceState != null && savedInstanceState.getBoolean(SAVED_PICKER);
        super.onCreate(savedInstanceState);
        if (!intentHandled) {
            intentHandled = true;
            receiveIntent(getIntent(), false);
        }
    }

    @Override
    public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        intentHandled = true;
        receiveIntent(intent, false);
    }

    @Override
    public void onSaveInstanceState(Bundle outState) {
        outState.putBoolean(SAVED_HANDLED, intentHandled);
        outState.putBoolean(SAVED_PICKER, pickerOpen);
        super.onSaveInstanceState(outState);
    }

    @Override
    public void onResume() {
        super.onResume();
        resumed = true;
        state.activity = new WeakReference<>(this);
        state.flush();
    }

    @Override
    public void onPause() {
        resumed = false;
        super.onPause();
    }

    @Override
    public void onDestroy() {
        resumed = false;
        if (state.activity.get() == this) {
            state.activity.clear();
            state.stopExecutorIfIdle();
        }
        // Accepted copies use application context and outlive rotation. A finishing Activity
        // may lose a transient grant; that becomes PERMISSION_DENIED, never a partial success.
        super.onDestroy();
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FILE) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        pickerOpen = false;
        if (resultCode == Activity.RESULT_OK)
            receiveIntent(data, true);
        // User cancellation is not an error and does not discard existing staged files.
    }

    private void receiveIntent(Intent intent, boolean fromPicker) {
        try {
            if (intent == null) {
                if (fromPicker)
                    state.fail(INVALID_INPUT);
                return;
            }
            String action = intent.getAction();
            if (!fromPicker && !Intent.ACTION_VIEW.equals(action) && !Intent.ACTION_SEND.equals(action)) {
                if (Intent.ACTION_SEND_MULTIPLE.equals(action))
                    state.fail(INVALID_INPUT);
                return;
            }
            Uri uri = intent.getData();
            if (intent.hasExtra(Intent.EXTRA_STREAM)) {
                Uri stream = IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri.class);
                if (stream == null)
                    throw new BridgeException(INVALID_INPUT);
                uri = sameUri(uri, stream);
            }
            ClipData clip = intent.getClipData();
            if (clip != null) {
                if (clip.getItemCount() != 1 || clip.getItemAt(0).getUri() == null)
                    throw new BridgeException(INVALID_INPUT);
                uri = sameUri(uri, clip.getItemAt(0).getUri());
            }
            if (uri == null || !"content".equals(uri.getScheme()) || uri.getAuthority() == null
                    || uri.getAuthority().isEmpty() || uri.toString().length() > 8192)
                throw new BridgeException(INVALID_INPUT);
            // Do not persist a capability or reinterpret its path. Copy while the temporary
            // Activity grant is valid. The system picker uses exactly this incoming path.
            state.accept(uri);
        } catch (BridgeException e) {
            state.fail(e.code);
        } catch (RuntimeException e) {
            // Includes malformed parcelables from an exported Activity entry point.
            state.fail(INVALID_INPUT);
        }
    }

    private static Uri sameUri(Uri first, Uri second) throws BridgeException {
        if (first != null && !first.equals(second))
            throw new BridgeException(INVALID_INPUT);
        return second;
    }

    // JNI entry points only post values to the main looper. Tokens prevent old C++ instances
    // and queued requests from driving a replacement receiver or a destroyed Activity.
    public static boolean attachNative(long receiver) {
        return state != null && MAIN.post(() -> {
            state.receiver = receiver;
            state.inFlight = null;
            state.flush();
        });
    }

    public static boolean detachNative(long receiver) {
        return state != null && MAIN.post(() -> {
            if (state.receiver == receiver) {
                state.receiver = 0;
                state.inFlight = null;
            }
        });
    }

    public static boolean acknowledge(long receiver, long eventId) {
        return state != null && MAIN.post(() -> {
            if (state.receiver != receiver || state.inFlight == null || state.inFlight.id != eventId)
                return;
            Event event = state.inFlight;
            state.inFlight = null;
            if (event.input != null)
                event.input.delivered = true;
            else if (state.error == event)
                state.error = null;
            state.flush();
        });
    }

    public static boolean chooseFile(long receiver) {
        return state != null && MAIN.post(() -> {
            if (state.receiver != receiver)
                return;
            LabelQtActivity activity = state.usableActivity();
            if (activity == null) {
                state.fail(UNAVAILABLE);
                return;
            }
            if (activity.pickerOpen) {
                state.fail(BUSY);
                return;
            }
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES, inputMimeTypes());
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, false);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try {
                activity.pickerOpen = true;
                activity.startActivityForResult(intent, PICK_FILE);
            } catch (RuntimeException e) {
                activity.pickerOpen = false;
                state.fail(PICKER_FAILED);
            }
        });
    }

    public static boolean shareFile(long receiver, String localFilePath) {
        return state != null && MAIN.post(() -> {
            if (state.receiver != receiver)
                return;
            LabelQtActivity activity = state.usableActivity();
            if (activity == null) {
                state.fail(UNAVAILABLE);
                return;
            }
            try {
                if (localFilePath == null || localFilePath.indexOf('\0') >= 0)
                    throw new BridgeException(SHARE_FAILED);
                File candidate = new File(localFilePath);
                File outgoing = new File(state.context.getFilesDir().getCanonicalFile(), "outgoing");
                File file = candidate.getCanonicalFile();
                File batch = file.getParentFile();
                if (!candidate.isAbsolute() || !outgoing.getCanonicalFile().equals(outgoing)
                        || !candidate.equals(file) || batch == null || !outgoing.equals(batch.getParentFile())
                        || !UUID.fromString(batch.getName()).toString().equals(batch.getName())
                        || !OsConstants.S_ISREG(Os.lstat(candidate.getAbsolutePath()).st_mode)
                        || !file.canRead())
                    throw new BridgeException(SHARE_FAILED);
                String suffix = safeExtension(file.getName());
                if (suffix == null)
                    throw new BridgeException(UNSUPPORTED_TYPE);
                Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".files", file);
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("zip".equals(suffix) ? "application/zip" : "text/plain");
                send.putExtra(Intent.EXTRA_STREAM, uri);
                send.setClipData(ClipData.newRawUri("", uri));
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                Intent chooser = Intent.createChooser(send, null);
                chooser.setClipData(send.getClipData());
                chooser.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                activity.startActivity(chooser);
                // No result means delivery, including a chooser selection or Activity return.
                // Receivers may read later; C++ maintenance expires exports only after seven days.
            } catch (BridgeException e) {
                state.fail(e.code);
            } catch (Exception e) {
                state.fail(SHARE_FAILED);
            }
        });
    }

    /** Explicit subclass for manifest registration; even accidental write grants cannot write. */
    public static final class ShareProvider extends FileProvider {
        @Override
        public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
            if (!"r".equals(mode))
                throw new FileNotFoundException("read-only");
            return super.openFile(uri, mode);
        }

        @Override
        public int delete(Uri uri, String selection, String[] selectionArgs) {
            throw new SecurityException("read-only");
        }
    }

    private static final class BridgeException extends IOException {
        private static final long serialVersionUID = 1L;

        final int code;

        BridgeException(int code) {
            this.code = code;
        }
    }

    private static final class Input {
        final Uri uri;
        File file;
        String displayName;
        Event event;
        boolean delivered;

        Input(Uri uri) {
            this.uri = uri;
        }
    }

    private static final class Event {
        final long id;
        final Input input;
        final int code;

        Event(long id, Input input, int code) {
            this.id = id;
            this.input = input;
            this.code = code;
        }
    }

    private static final class BridgeState {
        final Context context;
        final ArrayList<Input> inputs = new ArrayList<>();
        final ArrayDeque<Uri> startup = new ArrayDeque<>();
        WeakReference<LabelQtActivity> activity = new WeakReference<>(null);
        ThreadPoolExecutor executor;
        boolean initializing = true;
        boolean inboxUsable;
        int copying;
        long receiver;
        long nextEvent;
        Event error;
        Event inFlight;

        BridgeState(Context context) {
            this.context = context;
            execute(() -> {
                ArrayList<File> recovered = new ArrayList<>();
                int problem = 0;
                boolean usable = false;
                try {
                    File inbox = inboxDirectory(context);
                    File[] entries = inbox.listFiles();
                    if (entries == null)
                        throw new IOException();
                    long bytes = 0;
                    for (File file : entries) {
                        if (!isStagedRegularFile(file))
                            throw new IOException();
                        if (file.getName().endsWith(".part")) {
                            if (!file.delete())
                                throw new IOException();
                            problem = INTERRUPTED;
                        } else {
                            long size = file.length();
                            if (recovered.size() == MAX_PENDING || size > MAX_BYTES - bytes)
                                throw new BridgeException(TOO_LARGE);
                            bytes += size;
                            recovered.add(file);
                        }
                    }
                    usable = true;
                } catch (BridgeException e) {
                    problem = e.code;
                } catch (Exception e) {
                    problem = COPY_FAILED;
                }
                final int result = problem;
                final boolean ready = usable;
                MAIN.post(() -> {
                    initializing = false;
                    inboxUsable = ready;
                    if (ready) {
                        for (File file : recovered) {
                            Input input = new Input(null);
                            input.file = file;
                            input.displayName = "project." + safeExtension(file.getName());
                            input.event = new Event(++nextEvent, input, 0);
                            inputs.add(input);
                        }
                    }
                    if (result != 0)
                        fail(result);
                    while (!startup.isEmpty())
                        accept(startup.removeFirst());
                    flush();
                    stopExecutorIfIdle();
                });
            });
        }

        LabelQtActivity usableActivity() {
            LabelQtActivity current = activity.get();
            return current != null && current.resumed && !current.isFinishing() && !current.isDestroyed()
                    ? current : null;
        }

        void execute(Runnable task) {
            if (executor == null) {
                executor = new ThreadPoolExecutor(0, 1, 30, TimeUnit.SECONDS,
                        new ArrayBlockingQueue<>(MAX_PENDING), runnable -> {
                            Thread thread = new Thread(runnable, "LabelQt-inbox");
                            thread.setDaemon(true);
                            return thread;
                        });
            }
            executor.execute(task);
        }

        void stopExecutorIfIdle() {
            // A process-scoped queue survives rotation without retaining its Activity. Its
            // sole thread times out when idle; explicitly shut down after the last Activity.
            if (activity.get() == null && !initializing && copying == 0 && executor != null) {
                executor.shutdown();
                executor = null;
            }
        }

        void accept(Uri uri) {
            if (initializing) {
                if (startup.contains(uri))
                    return;
                if (startup.size() == MAX_PENDING)
                    fail(BUSY);
                else
                    startup.addLast(uri);
                return;
            }
            if (!inboxUsable) {
                fail(COPY_FAILED);
                return;
            }
            // The C++ transfer service releases quota by moving/deleting consumed files.
            inputs.removeIf(input -> input.delivered && input.file != null && !input.file.exists());
            for (Input input : inputs) {
                if (uri.equals(input.uri))
                    return;
            }
            if (inputs.size() == MAX_PENDING) {
                fail(BUSY);
                return;
            }
            Input input = new Input(uri);
            inputs.add(input);
            ++copying;
            try {
                execute(() -> {
                    StagedFile file = null;
                    int problem = 0;
                    try {
                        file = copyToInbox(context, uri);
                    } catch (BridgeException e) {
                        problem = e.code;
                    } catch (SecurityException e) {
                        problem = PERMISSION_DENIED;
                    } catch (Exception e) {
                        problem = COPY_FAILED;
                    }
                    final StagedFile result = file;
                    final int code = problem;
                    MAIN.post(() -> {
                        --copying;
                        if (code != 0) {
                            inputs.remove(input);
                            fail(code);
                        } else {
                            input.file = result.file;
                            input.displayName = result.displayName;
                            input.event = new Event(++nextEvent, input, 0);
                            flush();
                        }
                        stopExecutorIfIdle();
                    });
                });
            } catch (RejectedExecutionException e) {
                --copying;
                inputs.remove(input);
                fail(BUSY);
            }
        }

        void fail(int code) {
            // Coalesce an external Intent flood into one bounded error mailbox. Never drop
            // successful inputs or replace an event already queued to the native owner thread.
            if (error == null)
                error = new Event(++nextEvent, null, code);
            flush();
        }

        void flush() {
            if (receiver == 0 || inFlight != null)
                return;
            Event event = error;
            if (event == null) {
                for (Input input : inputs) {
                    if (!input.delivered && input.event != null) {
                        event = input.event;
                        break;
                    }
                }
            }
            if (event == null)
                return;
            inFlight = event;
            try {
                nativeResult(receiver, event.id, event.input == null ? null : event.input.file.getAbsolutePath(),
                        event.input == null ? null : event.input.displayName, event.code);
            } catch (LinkageError e) {
                // The library is not ready/unloaded; retain the event until the next attachment.
                receiver = 0;
                inFlight = null;
            }
        }
    }

    private static File inboxDirectory(Context context) throws IOException {
        File root = context.getFilesDir().getCanonicalFile();
        File inbox = new File(root, "inbox");
        if (!inbox.getCanonicalFile().equals(inbox) || (!inbox.isDirectory() && !inbox.mkdir()))
            throw new IOException();
        return inbox;
    }

    private static boolean isStagedRegularFile(File file) throws Exception {
        return STAGED_NAME.matcher(file.getName()).matches()
                && OsConstants.S_ISREG(Os.lstat(file.getAbsolutePath()).st_mode);
    }

    private static String safeExtension(String name) {
        if (name == null || name.length() > 240 || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0)
            return null;
        for (int i = 0; i < name.length(); ++i) {
            if (Character.isISOControl(name.charAt(i)))
                return null;
        }
        int dot = name.lastIndexOf('.');
        if (dot <= 0)
            return null;
        String extension = name.substring(dot + 1).toLowerCase(Locale.ROOT);
        for (String[] type : INPUT_TYPES)
            if (type[0].equals(extension))
                return extension;
        return null;
    }

    private static String inputName(ContentResolver resolver, Uri uri) throws IOException {
        // DISPLAY_NAME supplies a suffix hint and UI title, never a local destination path.
        try (Cursor cursor = resolver.query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                String suffix = column < 0 ? null : safeExtension(cursor.getString(column));
                if (suffix != null)
                    return cursor.getString(column);
            }
        }
        String mime = resolver.getType(uri);
        for (String[] type : INPUT_TYPES)
            if (type[1].equalsIgnoreCase(mime))
                return "project." + type[0];
        throw new BridgeException(UNSUPPORTED_TYPE);
    }

    private static final class StagedFile {
        final File file;
        final String displayName;

        StagedFile(File file, String displayName) {
            this.file = file;
            this.displayName = displayName;
        }
    }

    private static StagedFile copyToInbox(Context context, Uri uri) throws Exception {
        File inbox = inboxDirectory(context);
        File[] existing = inbox.listFiles();
        if (existing == null)
            throw new IOException();
        if (existing.length >= MAX_PENDING)
            throw new BridgeException(BUSY);
        long used = 0;
        for (File file : existing) {
            if (!isStagedRegularFile(file))
                throw new IOException();
            long size = file.length();
            if (size > MAX_BYTES - used)
                throw new BridgeException(TOO_LARGE);
            used += size;
        }
        ContentResolver resolver = context.getContentResolver();
        String displayName = inputName(resolver, uri);
        String suffix = safeExtension(displayName);
        String id = UUID.randomUUID().toString();
        File partial = new File(inbox, id + ".part");
        File complete = new File(inbox, id + "." + suffix);
        if (!partial.createNewFile())
            throw new IOException();
        boolean published = false;
        try {
            // Both the individual file and all retained inputs share this 2 GiB budget.
            // Ignore provider size claims: unknown-length and non-seekable streams work too.
            try (InputStream source = resolver.openInputStream(uri);
                    FileOutputStream target = new FileOutputStream(partial)) {
                if (source == null)
                    throw new IOException();
                byte[] buffer = new byte[64 * 1024];
                long copied = 0;
                int count;
                while ((count = source.read(buffer)) != -1) {
                    if (Thread.currentThread().isInterrupted())
                        throw new BridgeException(INTERRUPTED);
                    if (count == 0)
                        throw new IOException();
                    if (count > MAX_BYTES - used - copied)
                        throw new BridgeException(TOO_LARGE);
                    target.write(buffer, 0, count);
                    copied += count;
                }
                target.getFD().sync();
            }
            if (complete.exists() || !partial.renameTo(complete))
                throw new IOException();
            published = true;
            return new StagedFile(complete, displayName);
        } finally {
            // A failed cleanup remains a quota-counted .part, recoverable on process restart.
            if (!published)
                partial.delete();
        }
    }
}
