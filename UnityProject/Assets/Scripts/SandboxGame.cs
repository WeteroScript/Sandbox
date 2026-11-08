using System.Collections.Generic;
using UnityEngine;

namespace Sandbox.UnityPort
{
    public enum SandboxTool
    {
        Grab,
        Freeze,
        Clone,
        Delete
    }

    public sealed class SandboxGame : MonoBehaviour
    {
        private readonly List<SandboxBody> spawned = new List<SandboxBody>();
        private SandboxTool tool = SandboxTool.Grab;
        private SandboxBody heldBody;
        private Vector2 grabOffset;
        private bool spawnPanel;
        private float simulationSpeed = 1f;
        private Camera sceneCamera;
        private GUIStyle buttonStyle;
        private GUIStyle titleStyle;

        private void Start()
        {
            sceneCamera = Camera.main;
            if (sceneCamera == null)
                sceneCamera = FindObjectOfType<Camera>();

            CreateArena();
            SpawnCharacter(new Vector2(-3f, -1.2f));
            SpawnBox(new Vector2(2f, -2f));
            SpawnBall(new Vector2(3f, 1f));
        }

        private void Update()
        {
            Time.timeScale = simulationSpeed;
            HandleKeyboard();
            HandlePointer();
            if (heldBody != null && tool == SandboxTool.Grab)
            {
                Vector2 pointer = GetWorldPointer();
                heldBody.transform.position = pointer + grabOffset;
                var body = heldBody.GetComponent<Rigidbody2D>();
                if (body != null)
                    body.velocity = Vector2.zero;
            }
        }

        private void OnDestroy()
        {
            Time.timeScale = 1f;
        }

        private void HandleKeyboard()
        {
            if (Input.GetKeyDown(KeyCode.G)) tool = SandboxTool.Grab;
            if (Input.GetKeyDown(KeyCode.V)) tool = SandboxTool.Freeze;
            if (Input.GetKeyDown(KeyCode.B)) tool = SandboxTool.Clone;
            if (Input.GetKeyDown(KeyCode.J)) tool = SandboxTool.Delete;
            if (Input.GetKeyDown(KeyCode.Comma)) ChangeSpeed(-1);
            if (Input.GetKeyDown(KeyCode.Period)) ChangeSpeed(1);
            if (Input.GetKeyDown(KeyCode.Escape)) spawnPanel = false;
        }

        private void HandlePointer()
        {
            bool down = Input.GetMouseButtonDown(0);
            bool up = Input.GetMouseButtonUp(0);
            if (Input.touchCount > 0)
            {
                down |= Input.GetTouch(0).phase == TouchPhase.Began;
                up |= Input.GetTouch(0).phase == TouchPhase.Ended ||
                      Input.GetTouch(0).phase == TouchPhase.Canceled;
            }

            if (down && !IsPointerOnHud())
            {
                Vector2 world = GetWorldPointer();
                var hit = Physics2D.OverlapPoint(world);
                var selected = hit != null ? hit.GetComponentInParent<SandboxBody>() : null;
                if (selected == null)
                    return;

                switch (tool)
                {
                    case SandboxTool.Grab:
                        heldBody = selected;
                        grabOffset = (Vector2)selected.transform.position - world;
                        break;
                    case SandboxTool.Freeze:
                        selected.SetFrozen(!selected.IsFrozen);
                        break;
                    case SandboxTool.Clone:
                        var duplicate = selected.CreateDuplicate(
                            (Vector2)selected.transform.position + Vector2.right * 1.1f);
                        Register(duplicate);
                        break;
                    case SandboxTool.Delete:
                        if (selected.Kind != SandboxBodyKind.Static)
                        {
                            spawned.Remove(selected);
                            Destroy(selected.gameObject);
                        }
                        break;
                }
            }

            if (up)
                heldBody = null;
        }

        private bool IsPointerOnHud()
        {
            float scale = Mathf.Clamp(Screen.width / 900f, 0.75f, 1.25f);
            Vector2 screenPoint = Input.touchCount > 0
                ? Input.GetTouch(0).position
                : (Vector2)Input.mousePosition;
            float x = screenPoint.x / scale;
            float y = (Screen.height - screenPoint.y) / scale;

            if (y <= 76f || y >= Screen.height / scale - 76f)
                return true;

            if (spawnPanel && x >= Screen.width / scale - 235f && y >= 70f && y <= 400f)
                return true;

            return false;
        }

        private Vector2 GetWorldPointer()
        {
            Vector3 screen = Input.touchCount > 0
                ? (Vector3)Input.GetTouch(0).position
                : Input.mousePosition;
            return sceneCamera.ScreenToWorldPoint(screen);
        }

        private void CreateArena()
        {
            CreateBlock("Ground", new Vector2(0f, -5.1f), new Vector2(24f, 0.8f),
                new Color(0.28f, 0.32f, 0.37f), true);
            CreateBlock("Left Wall", new Vector2(-12.2f, 0f), new Vector2(0.8f, 12f),
                new Color(0.25f, 0.29f, 0.34f), true);
            CreateBlock("Right Wall", new Vector2(12.2f, 0f), new Vector2(0.8f, 12f),
                new Color(0.25f, 0.29f, 0.34f), true);
        }

        private SandboxBody CreateBlock(string objectName, Vector2 position, Vector2 size,
            Color color, bool isStatic)
        {
            var go = new GameObject(objectName);
            go.transform.position = position;
            go.transform.localScale = size;
            var renderer = go.AddComponent<SpriteRenderer>();
            renderer.sprite = CreateWhiteSprite();
            renderer.color = color;
            go.AddComponent<BoxCollider2D>();
            var rigidbody = go.AddComponent<Rigidbody2D>();
            rigidbody.bodyType = isStatic ? RigidbodyType2D.Static : RigidbodyType2D.Dynamic;
            var body = go.AddComponent<SandboxBody>();
            body.Kind = isStatic ? SandboxBodyKind.Static : SandboxBodyKind.Box;
            body.Tint = color;
            if (!isStatic)
                Register(body);
            return body;
        }

        private void SpawnBox(Vector2 position)
        {
            var body = CreateBlock("Crate", position, Vector2.one,
                new Color(0.72f, 0.48f, 0.25f), false);
            body.Kind = SandboxBodyKind.Box;
        }

        private void SpawnBall(Vector2 position)
        {
            var go = new GameObject("Ball");
            go.transform.position = position;
            var renderer = go.AddComponent<SpriteRenderer>();
            renderer.sprite = CreateWhiteSprite();
            renderer.color = new Color(0.3f, 0.72f, 0.9f);
            go.transform.localScale = Vector3.one * 0.85f;
            go.AddComponent<CircleCollider2D>();
            go.AddComponent<Rigidbody2D>();
            var body = go.AddComponent<SandboxBody>();
            body.Kind = SandboxBodyKind.Ball;
            body.Tint = renderer.color;
            Register(body);
        }

        private void SpawnCharacter(Vector2 origin)
        {
            var melon = new Color(0.36f, 0.78f, 0.39f);
            var torso = CreateLimb("Melon Torso", origin, new Vector2(0.72f, 1.15f), melon, false);
            var head = CreateLimb("Melon Head", origin + Vector2.up * 1.05f,
                Vector2.one * 0.72f, melon, true);
            var leftArm = CreateLimb("Left Arm", origin + new Vector2(-0.62f, 0.3f),
                new Vector2(0.28f, 0.85f), melon, false);
            var rightArm = CreateLimb("Right Arm", origin + new Vector2(0.62f, 0.3f),
                new Vector2(0.28f, 0.85f), melon, false);
            var leftLeg = CreateLimb("Left Leg", origin + new Vector2(-0.23f, -0.9f),
                new Vector2(0.32f, 0.9f), melon, false);
            var rightLeg = CreateLimb("Right Leg", origin + new Vector2(0.23f, -0.9f),
                new Vector2(0.32f, 0.9f), melon, false);

            Connect(torso, head, new Vector2(0f, 0.58f), new Vector2(0f, -0.3f));
            Connect(torso, leftArm, new Vector2(-0.34f, 0.38f), new Vector2(0f, 0.38f));
            Connect(torso, rightArm, new Vector2(0.34f, 0.38f), new Vector2(0f, 0.38f));
            Connect(torso, leftLeg, new Vector2(-0.2f, -0.52f), new Vector2(0f, 0.42f));
            Connect(torso, rightLeg, new Vector2(0.2f, -0.52f), new Vector2(0f, 0.42f));
        }

        private SandboxBody CreateLimb(string name, Vector2 position, Vector2 size,
            Color color, bool round)
        {
            var go = new GameObject(name);
            go.transform.position = position;
            go.transform.localScale = size;
            var renderer = go.AddComponent<SpriteRenderer>();
            renderer.sprite = CreateWhiteSprite();
            renderer.color = color;
            if (round) go.AddComponent<CircleCollider2D>();
            else go.AddComponent<CapsuleCollider2D>();
            var rigidbody = go.AddComponent<Rigidbody2D>();
            rigidbody.mass = 1.8f;
            rigidbody.gravityScale = 1.4f;
            var body = go.AddComponent<SandboxBody>();
            body.Kind = SandboxBodyKind.CharacterPart;
            body.Tint = color;
            Register(body);
            return body;
        }

        private void Connect(SandboxBody parent, SandboxBody child,
            Vector2 parentAnchor, Vector2 childAnchor)
        {
            var joint = parent.gameObject.AddComponent<HingeJoint2D>();
            joint.connectedBody = child.GetComponent<Rigidbody2D>();
            joint.autoConfigureConnectedAnchor = false;
            joint.anchor = parentAnchor;
            joint.connectedAnchor = childAnchor;
            joint.enableCollision = false;
        }

        private void Register(SandboxBody body)
        {
            if (body != null && !spawned.Contains(body))
                spawned.Add(body);
        }

        private void ChangeSpeed(int direction)
        {
            float[] speeds = { 0.25f, 0.5f, 1f, 2f };
            int index = System.Array.IndexOf(speeds, simulationSpeed);
            simulationSpeed = speeds[Mathf.Clamp(index + direction, 0, speeds.Length - 1)];
        }

        private void OnGUI()
        {
            if (buttonStyle == null)
            {
                buttonStyle = new GUIStyle(GUI.skin.button)
                {
                    fontSize = 18,
                    fontStyle = FontStyle.Bold,
                    fixedHeight = 48
                };
                titleStyle = new GUIStyle(GUI.skin.label)
                {
                    fontSize = 22,
                    fontStyle = FontStyle.Bold,
                    normal = { textColor = Color.white }
                };
            }

            float scale = Mathf.Clamp(Screen.width / 900f, 0.75f, 1.25f);
            GUI.matrix = Matrix4x4.TRS(Vector3.zero, Quaternion.identity, Vector3.one * scale);
            GUILayout.BeginArea(new Rect(12, 10, 860, 58));
            GUILayout.BeginHorizontal();
            GUILayout.Label("SANDBOX", titleStyle, GUILayout.Width(150));
            if (GUILayout.Button("−", buttonStyle, GUILayout.Width(52))) ChangeSpeed(-1);
            GUILayout.Label(simulationSpeed.ToString("0.##") + "x", titleStyle, GUILayout.Width(65));
            if (GUILayout.Button("+", buttonStyle, GUILayout.Width(52))) ChangeSpeed(1);
            GUILayout.FlexibleSpace();
            if (GUILayout.Button("SPAWN", buttonStyle, GUILayout.Width(110))) spawnPanel = !spawnPanel;
            GUILayout.EndHorizontal();
            GUILayout.EndArea();

            GUILayout.BeginArea(new Rect(12, Screen.height / scale - 66, 520, 58));
            GUILayout.BeginHorizontal();
            DrawToolButton("GRAB", SandboxTool.Grab);
            DrawToolButton("FREEZE", SandboxTool.Freeze);
            DrawToolButton("CLONE", SandboxTool.Clone);
            DrawToolButton("DELETE", SandboxTool.Delete);
            GUILayout.EndHorizontal();
            GUILayout.EndArea();

            if (spawnPanel)
            {
                GUILayout.BeginArea(new Rect(Screen.width / scale - 220, 82, 205, 300), GUI.skin.box);
                GUILayout.Label("SPAWN OBJECT", titleStyle);
                if (GUILayout.Button("BOX", buttonStyle)) SpawnBox(new Vector2(0f, 1f));
                if (GUILayout.Button("BALL", buttonStyle)) SpawnBall(new Vector2(0f, 1f));
                if (GUILayout.Button("MELON", buttonStyle)) SpawnCharacter(new Vector2(0f, 1f));
                if (GUILayout.Button("CLEAR ITEMS", buttonStyle)) ClearSpawned();
                GUILayout.EndArea();
            }
        }

        private void DrawToolButton(string label, SandboxTool value)
        {
            Color old = GUI.backgroundColor;
            GUI.backgroundColor = tool == value ? new Color(0.28f, 0.72f, 0.45f) : Color.white;
            if (GUILayout.Button(label, buttonStyle, GUILayout.Width(124)))
                tool = value;
            GUI.backgroundColor = old;
        }

        private void ClearSpawned()
        {
            heldBody = null;
            foreach (var body in spawned)
                if (body != null) Destroy(body.gameObject);
            spawned.Clear();
        }

        private static Sprite CreateWhiteSprite()
        {
            if (whiteSprite != null)
                return whiteSprite;

            var texture = new Texture2D(2, 2, TextureFormat.RGBA32, false);
            texture.SetPixels(new[] { Color.white, Color.white, Color.white, Color.white });
            texture.Apply();
            whiteSprite = Sprite.Create(texture, new Rect(0, 0, 2, 2), new Vector2(0.5f, 0.5f), 1f);
            return whiteSprite;
        }

        private static Sprite whiteSprite;
    }
}
