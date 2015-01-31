package com.softmob.gles3jni;

import android.content.res.AssetManager;

public class GLES3JNILib {

     static {
         System.loadLibrary("gles3jni");
     }

     public static native void init();
     public static native void resize(int width, int height);
     public static native void render();
     public static native void nativeSetAssetManager(AssetManager mng);
}