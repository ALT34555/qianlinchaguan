package org.qianlinchaguan;

import android.app.Activity;
import android.content.Intent;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.net.Uri;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.Executors;
import org.json.*;

public final class QlPlatform {
    private static final int IMPORT=701, EXPORT=702;
    private static final long LIMIT=512L*1024*1024;
    private static final HashMap<String,Typeface> fonts=new HashMap<>();
    private static final ArrayDeque<String[]> completed=new ArrayDeque<>();
    private static String exportPath;
    private static boolean pending;
    private static native boolean bind(Activity activity,String resources,String data,String cache);

    public static boolean prepare(Activity activity) throws Exception {
        byte[] manifest;
        try(InputStream in=activity.getAssets().open("resources.json")) { manifest=read(in,4*1024*1024); }
        String version=hex(MessageDigest.getInstance("SHA-256").digest(manifest));
        File root=new File(activity.getFilesDir(),"package-"+version);
        File marker=new File(root,"ready");
        if(!marker.isFile()) {
            JSONObject catalog=new JSONObject(new String(manifest,StandardCharsets.UTF_8));
            if(catalog.getInt("version")!=1) throw new IOException("Resource manifest version");
            JSONArray files=catalog.getJSONArray("files");
            for(int i=0;i<files.length();i++) {
                JSONObject entry=files.getJSONObject(i);
                String path=entry.getString("path");
                File target=new File(root,path).getCanonicalFile();
                if(!target.getPath().startsWith(root.getCanonicalPath()+File.separator)) throw new IOException("Resource path");
                target.getParentFile().mkdirs();
                File temp=new File(target.getPath()+".new");
                MessageDigest digest=MessageDigest.getInstance("SHA-256");
                long bytes=0;
                try(InputStream in=activity.getAssets().open(path); FileOutputStream out=new FileOutputStream(temp)) {
                    byte[] buffer=new byte[16384]; int n;
                    while((n=in.read(buffer))!=-1) { bytes+=n; if(bytes>LIMIT) throw new IOException("Resource limit"); out.write(buffer,0,n); digest.update(buffer,0,n); }
                    out.getFD().sync();
                }
                if(bytes!=entry.getLong("bytes") || !hex(digest.digest()).equals(entry.getString("sha256"))) throw new IOException("Resource checksum");
                if(!temp.renameTo(target)) throw new IOException("Resource commit");
            }
            try(FileOutputStream out=new FileOutputStream(marker)) { out.write(1); out.getFD().sync(); }
        }
        File data=new File(activity.getFilesDir(),"userdata"); data.mkdirs();
        new File(data,"saves").mkdirs(); new File(data,"mods/fonts").mkdirs(); new File(data,"mods/l10n").mkdirs();
        return bind(activity,root.getPath(),activity.getFilesDir().getPath(),activity.getCacheDir().getPath());
    }
    private static byte[] read(InputStream in,int limit) throws IOException {
        ByteArrayOutputStream out=new ByteArrayOutputStream(); byte[] bytes=new byte[16384]; int n;
        while((n=in.read(bytes))!=-1) { if(out.size()>limit-n) throw new IOException("Size limit"); out.write(bytes,0,n); }
        return out.toByteArray();
    }
    private static String hex(byte[] bytes) {
        StringBuilder out=new StringBuilder(); for(byte b:bytes) out.append(String.format(Locale.ROOT,"%02x",b&255)); return out.toString();
    }
    public static synchronized boolean addFont(String id,String path) {
        try { fonts.put(id,Typeface.createFromFile(path)); return true; } catch(RuntimeException error) { return false; }
    }
    public static synchronized void clearFonts() { fonts.clear(); }
    public static synchronized Bitmap text(String text,String family,int px,boolean bold) {
        if(family.equals("Consolas")) family="monospace";
        if(family.equals("Microsoft YaHei UI")) family="sans-serif";
        Typeface base=fonts.get(family);
        if(base==null) base=Typeface.create(family,Typeface.NORMAL);
        Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG); paint.setTypeface(Typeface.create(base,bold?Typeface.BOLD:Typeface.NORMAL));
        paint.setTextSize(px); paint.setColor(Color.WHITE);
        Paint.FontMetrics metrics=paint.getFontMetrics();
        int width=Math.max(4,Math.min(1020,(int)Math.ceil(paint.measureText(text))+4));
        int height=Math.max(4,Math.min(216,(int)Math.ceil(metrics.descent-metrics.ascent)+2));
        Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
        new Canvas(bitmap).drawText(text,1,1-metrics.ascent,paint); return bitmap;
    }
    public static Bitmap image(String path) {
        BitmapFactory.Options options=new BitmapFactory.Options(); options.inJustDecodeBounds=true;
        BitmapFactory.decodeFile(path,options);
        if(options.outWidth<1 || options.outHeight<1 || options.outWidth>8192 || options.outHeight>8192 || (long)options.outWidth*options.outHeight>16777216) return null;
        options.inJustDecodeBounds=false; options.inPreferredConfig=Bitmap.Config.ARGB_8888;
        options.inPremultiplied=false; options.inScaled=false;
        return BitmapFactory.decodeFile(path,options);
    }
    public static boolean png(String path,int[] pixels,int width,int height) {
        Bitmap bitmap=null;
        try(FileOutputStream out=new FileOutputStream(path)) {
            bitmap=Bitmap.createBitmap(pixels,width,height,Bitmap.Config.ARGB_8888);
            boolean ok=bitmap.compress(Bitmap.CompressFormat.PNG,100,out); out.getFD().sync(); return ok;
        } catch(IOException|RuntimeException error) { return false; }
        finally { if(bitmap!=null) bitmap.recycle(); }
    }
    public static void requestImport(Activity activity) {
        activity.runOnUiThread(()-> {
            synchronized(QlPlatform.class) { if(pending) return; pending=true; }
            Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            try { activity.startActivityForResult(intent,IMPORT); } catch(RuntimeException e) { documentError("document.openFailed",e); }
        });
    }
    public static void requestExport(Activity activity,String path) {
        synchronized(QlPlatform.class) { if(pending) return; pending=true; }
        File snapshot=new File(activity.getCacheDir(),"export-"+UUID.randomUUID()+".sav");
        try(InputStream in=new FileInputStream(path); FileOutputStream out=new FileOutputStream(snapshot)) {
            transfer(in,out); out.getFD().sync();
        } catch(Exception error) { snapshot.delete(); documentError("document.exportFailed",error); return; }
        synchronized(QlPlatform.class) { exportPath=snapshot.getPath(); }
        activity.runOnUiThread(()-> {
            Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream");
            intent.putExtra(Intent.EXTRA_TITLE,new File(path).getName());
            try { activity.startActivityForResult(intent,EXPORT); } catch(RuntimeException e) { documentError("document.openFailed",e); }
        });
    }
    public static boolean onActivityResult(Activity activity,int request,int code,Intent intent) {
        if(request!=IMPORT && request!=EXPORT) return false;
        if(code!=Activity.RESULT_OK || intent==null || intent.getData()==null) { result("cancel","",""); return true; }
        Uri uri=intent.getData(); final String source;
        synchronized(QlPlatform.class) { source=exportPath; }
        java.util.concurrent.ExecutorService worker=Executors.newSingleThreadExecutor();
        worker.execute(()-> {
            File staged=new File(activity.getCacheDir(),"import-"+UUID.randomUUID()+".sav");
            try {
                if(request==IMPORT) {
                    try(InputStream in=activity.getContentResolver().openInputStream(uri); FileOutputStream out=new FileOutputStream(staged)) {
                        transfer(in,out); out.getFD().sync();
                    }
                    result("import",staged.getPath(),"");
                } else {
                    try(InputStream in=new FileInputStream(source); OutputStream out=activity.getContentResolver().openOutputStream(uri,"wt")) { transfer(in,out); }
                    result("export",source,"");
                }
            } catch(Exception error) {
                if(request==IMPORT) { staged.delete();result("error","",error.toString()); }
                else documentError("document.exportFailed",error);
            }
            finally { worker.shutdown(); }
        });
        return true;
    }
    private static void transfer(InputStream in,OutputStream out) throws IOException {
        if(in==null || out==null) throw new IOException("Document stream unavailable");
        byte[] bytes=new byte[16384]; long total=0; int n;
        while((n=in.read(bytes))!=-1) { total+=n; if(total>LIMIT) throw new IOException("Document size limit"); out.write(bytes,0,n); }
        out.flush();
    }
    private static synchronized void result(String kind,String path,String error) {
        if(exportPath!=null) { new File(exportPath).delete(); exportPath=null; }
        completed.add(new String[]{kind,path,error}); pending=false;
    }
    private static void documentError(String key,Exception error) {
        android.util.Log.w("QlPlatform","Document operation failed",error);
        result("error","",key);
    }
    public static synchronized String[] pollDocument() { return completed.poll(); }
    public static String clipboard(Activity activity) {
        ClipboardManager manager=activity.getSystemService(ClipboardManager.class);
        ClipData data=manager.getPrimaryClip();
        if(data==null || data.getItemCount()==0) return "";
        CharSequence text=data.getItemAt(0).coerceToText(activity); return text==null?"":text.toString();
    }
    public static void clipboardSet(Activity activity,String text) {
        activity.getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("",text));
    }
    public static boolean dark(Activity activity) {
        int mode=activity.getResources().getConfiguration().uiMode;
        return (mode&android.content.res.Configuration.UI_MODE_NIGHT_MASK)==android.content.res.Configuration.UI_MODE_NIGHT_YES;
    }
}
