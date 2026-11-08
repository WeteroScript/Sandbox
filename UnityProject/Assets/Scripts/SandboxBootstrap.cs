using UnityEngine;

namespace Sandbox.UnityPort
{
    public static class SandboxBootstrap
    {
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void CreateRuntime()
        {
            if (Object.FindObjectOfType<SandboxGame>() != null)
                return;

            var cameraObject = new GameObject("Sandbox Camera");
            var camera = cameraObject.AddComponent<Camera>();
            camera.orthographic = true;
            camera.orthographicSize = 7.5f;
            camera.backgroundColor = new Color(0.12f, 0.15f, 0.19f);
            camera.transform.position = new Vector3(0f, 0f, -10f);
            cameraObject.tag = "MainCamera";

            var gameObject = new GameObject("Sandbox Game");
            gameObject.AddComponent<SandboxGame>();
        }
    }
}
