#if UNITY_EDITOR
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Sandbox.UnityPort.Editor
{
    public static class SandboxProjectSetup
    {
        [MenuItem("Sandbox/Setup Project and Android")]
        public static void SetupProject()
        {
            PlayerSettings.companyName = "WeteroScript";
            PlayerSettings.productName = "Sandbox";
            PlayerSettings.SetApplicationIdentifier(
                BuildTargetGroup.Android, "com.weteroscript.sandbox");
            PlayerSettings.defaultInterfaceOrientation = UIOrientation.LandscapeLeft;
            PlayerSettings.allowedAutorotateToLandscapeLeft = true;
            PlayerSettings.allowedAutorotateToLandscapeRight = true;
            PlayerSettings.allowedAutorotateToPortrait = false;
            PlayerSettings.allowedAutorotateToPortraitUpsideDown = false;
            PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel23;
            PlayerSettings.Android.targetSdkVersion = AndroidSdkVersions.AndroidApiLevelAuto;
            PlayerSettings.colorSpace = ColorSpace.Linear;

            var scene = EditorSceneManager.NewScene(
                NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var cameraObject = new GameObject("Sandbox Camera");
            var camera = cameraObject.AddComponent<Camera>();
            camera.orthographic = true;
            camera.orthographicSize = 7.5f;
            cameraObject.tag = "MainCamera";

            var bootstrap = new GameObject("Sandbox Runtime");
            bootstrap.AddComponent<SandboxGame>();

            const string scenePath = "Assets/Scenes/Sandbox.unity";
            System.IO.Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, scenePath);
            EditorBuildSettings.scenes = new[]
            {
                new EditorBuildSettingsScene(scenePath, true)
            };

            AssetDatabase.SaveAssets();
            Debug.Log("Sandbox scene and Android player settings configured.");
        }
    }
}
#endif
