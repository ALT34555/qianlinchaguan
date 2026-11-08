package org.qianlinchaguan;
import android.os.Bundle;
import android.content.Intent;
import android.view.View;
import com.google.androidgamesdk.GameActivity;
public final class MainActivity extends GameActivity {
 private static native void back();
 @Override public void onBackPressed() { back(); }
 private static native void configure(float density,int left,int top,int right,int bottom);
 @Override public androidx.core.view.WindowInsetsCompat onApplyWindowInsets(View view,androidx.core.view.WindowInsetsCompat insets) {
  androidx.core.graphics.Insets bars=insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.ime());
  configure(getResources().getDisplayMetrics().density,bars.left,bars.top,bars.right,bars.bottom);
  return super.onApplyWindowInsets(view,insets);
 }
 static { System.loadLibrary("qlcg_game"); }
 @Override protected void onCreate(Bundle state) {
  try { if(!QlPlatform.prepare(this)) throw new IllegalStateException("Resource binding failed"); }
  catch(Exception e) { throw new IllegalStateException(e); }
  configure(getResources().getDisplayMetrics().density,0,0,0,0);
  super.onCreate(state);
  androidx.core.view.WindowCompat.setDecorFitsSystemWindows(getWindow(),false);
  android.view.WindowManager.LayoutParams params=getWindow().getAttributes();
  params.layoutInDisplayCutoutMode=android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
  getWindow().setAttributes(params);
  fullScreen();
  getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
 }
 private void fullScreen() {
  androidx.core.view.WindowInsetsControllerCompat controller=androidx.core.view.WindowCompat.getInsetsController(getWindow(),getWindow().getDecorView());
  controller.setSystemBarsBehavior(androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
  controller.hide(androidx.core.view.WindowInsetsCompat.Type.systemBars());
 }
 @Override public void onWindowFocusChanged(boolean focused) {
  super.onWindowFocusChanged(focused);if(focused)fullScreen();
 }
 @Override protected void onActivityResult(int request,int result,Intent data) {
  if(!QlPlatform.onActivityResult(this,request,result,data)) super.onActivityResult(request,result,data);
 }
}
