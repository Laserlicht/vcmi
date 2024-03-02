package eu.vcmi.vcmi.util;

import android.annotation.TargetApi;
import android.content.res.AssetManager;
import android.os.Environment;
import android.text.TextUtils;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.Charset;

import eu.vcmi.vcmi.Const;

/**
 * @author F
 */
@TargetApi(Const.SUPPRESS_TRY_WITH_RESOURCES_WARNING)
public class FileUtil
{
    private static final int BUFFER_SIZE = 4096;

    public static String read(final InputStream stream) throws IOException
    {
        try (InputStreamReader reader = new InputStreamReader(stream))
        {
            return readInternal(reader);
        }
    }

    public static String read(final File file) throws IOException
    {
        try (FileReader reader = new FileReader(file))
        {
            return readInternal(reader);
        }
        catch (final FileNotFoundException ignored)
        {
            Log.w("Could not load file: " + file);
            return null;
        }
    }

    private static String readInternal(final InputStreamReader reader) throws IOException
    {
        final char[] buffer = new char[BUFFER_SIZE];
        int currentRead;
        final StringBuilder content = new StringBuilder();
        while ((currentRead = reader.read(buffer, 0, BUFFER_SIZE)) >= 0)
        {
            content.append(buffer, 0, currentRead);
        }
        return content.toString();
    }

    public static void write(final File file, final String data) throws IOException
    {
        if (!ensureWriteable(file))
        {
            Log.e("Couldn't write " + data + " to " + file);
            return;
        }
        try (final FileWriter fw = new FileWriter(file, false))
        {
            Log.v(null, "Saving data: " + data + " to " + file.getAbsolutePath());
            fw.write(data);
        }
    }

    private static boolean ensureWriteable(final File file)
    {
        if (file == null)
        {
            Log.e("Broken path given to fileutil::ensureWriteable");
            return false;
        }

        final File dir = file.getParentFile();

        if (dir.exists() || dir.mkdirs())
        {
            return true;
        }

        Log.e("Couldn't create dir " + dir);

        return false;
    }

    public static boolean clearDirectory(final File dir)
    {
        if (dir == null || dir.listFiles() == null)
        {
            Log.e("Broken path given to fileutil::clearDirectory");
            return false;
        }

        for (final File f : dir.listFiles())
        {
            if (f.isDirectory() && !clearDirectory(f))
            {
                return false;
            }

            if (!f.delete())
            {
                return false;
            }
        }
        return true;
    }

    public static void copyDir(final File srcFile, final File dstFile)
    {
        File[] files = srcFile.listFiles();

        if(!dstFile.exists()) dstFile.mkdir();

        if(files == null)
            return;

        for (File child : files){
            File childTarget = new File(dstFile, child.getName());

            if(child.isDirectory()){
                copyDir(child, childTarget);
            }
            else{
                copyFile(child, childTarget);
            }
        }
    }

    public static boolean copyFile(final File srcFile, final File dstFile)
    {
        if (!srcFile.exists())
        {
            return false;
        }

        final File dstDir = dstFile.getParentFile();
        if (!dstDir.exists())
        {
            if (!dstDir.mkdirs())
            {
                Log.w("Couldn't create dir to copy file: " + dstFile);
                return false;
            }
        }

        try (final FileInputStream input = new FileInputStream(srcFile);
             final FileOutputStream output = new FileOutputStream(dstFile))
        {
            copyStream(input, output);
            return true;
        }
        catch (final Exception ex)
        {
            Log.e("Couldn't copy " + srcFile + " to " + dstFile, ex);
            return false;
        }
    }

    public static void copyStream(InputStream source, OutputStream target) throws IOException
    {
        final byte[] buffer = new byte[BUFFER_SIZE];
        int read;
        while ((read = source.read(buffer)) != -1)
        {
            target.write(buffer, 0, read);
        }
    }

    public static String configFileLocation(File filesDir)
    {
        return filesDir + "/config/settings.json";
    }

    public static String readAssetsStream(final AssetManager assets, final String assetPath)
    {
        if (assets == null || TextUtils.isEmpty(assetPath))
        {
            return null;
        }

        try (java.util.Scanner s = new java.util.Scanner(assets.open(assetPath), "UTF-8").useDelimiter("\\A"))
        {
            return s.hasNext() ? s.next() : null;
        }
        catch (final IOException e)
        {
            Log.e("Couldn't read stream data", e);
            return null;
        }
    }
}
