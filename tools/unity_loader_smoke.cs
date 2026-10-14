// Copy to Assets/Editor/CompilerSmoke.cs in a disposable Unity project.
// Copy tests/unity_loader_smoke.shader to Assets/Smoke.shader.
using System;
using System.IO;
using UnityEngine;
using UnityEditor;
using System.Collections.Generic;
public static class CompilerSmoke {
 static bool failed;
 static void Track(string text, string stack, LogType type) { if(type == LogType.Error || type == LogType.Exception) failed = true; }
 public static void Run() {
  try {
   Application.logMessageReceived += Track;
   var output = "Bundles";
   Directory.CreateDirectory(output);
   var assets = new List<string>((Environment.GetEnvironmentVariable("FXC2_SMOKE_ASSETS") ?? "Assets/Smoke.shader").Split(';'));
   // Always-included built-ins otherwise become references to the player's
   // prebuilt shaders, so a material alone would not test their compilation.
   var graphics = new SerializedObject(AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/GraphicsSettings.asset")[0]);
   graphics.FindProperty("m_AlwaysIncludedShaders").arraySize = 0;
   graphics.ApplyModifiedPropertiesWithoutUndo();
   AssetDatabase.SaveAssets();
   int index = 0;
   foreach(var name in (Environment.GetEnvironmentVariable("FXC2_SMOKE_BUILTINS") ?? "").Split(';')) {
    if(name.Length == 0) continue;
    var builtin = Shader.Find(name);
    if(builtin == null) throw new Exception("Missing built-in shader " + name);
    var path = "Assets/Builtin" + index++ + ".mat";
    AssetDatabase.DeleteAsset(path);
    AssetDatabase.CreateAsset(new Material(builtin), path);
    assets.Add(path);
   }
   var bundle = new AssetBundleBuild { assetBundleName = "smoke", assetNames = assets.ToArray() };
   var manifest = BuildPipeline.BuildAssetBundles(output, new[] { bundle }, BuildAssetBundleOptions.ForceRebuildAssetBundle, BuildTarget.StandaloneWindows64);
   if (manifest == null || failed) throw new Exception("Bundle failed");
   var shader = AssetDatabase.LoadAssetAtPath<Shader>("Assets/Smoke.shader");
   foreach(var message in ShaderUtil.GetShaderMessages(shader))
    if (message.severity == UnityEditor.Rendering.ShaderCompilerMessageSeverity.Error) throw new Exception(message.message);
   Debug.Log("[CompilerSmoke] PASS fxc2 Windows shader bundle");
  } catch(Exception e) { Debug.LogException(e); EditorApplication.Exit(1); }
 }
}
