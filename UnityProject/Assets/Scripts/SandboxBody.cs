using UnityEngine;

namespace Sandbox.UnityPort
{
    public enum SandboxBodyKind
    {
        Box,
        Ball,
        Barrel,
        CharacterPart,
        Static
    }

    [RequireComponent(typeof(Rigidbody2D))]
    public sealed class SandboxBody : MonoBehaviour
    {
        public SandboxBodyKind Kind;
        public Color Tint = Color.white;
        public bool IsFrozen { get; private set; }

        private Rigidbody2D body;
        private Collider2D bodyCollider;
        private SpriteRenderer spriteRenderer;
        private RigidbodyType2D previousBodyType;

        private void Awake()
        {
            body = GetComponent<Rigidbody2D>();
            bodyCollider = GetComponent<Collider2D>();
            spriteRenderer = GetComponent<SpriteRenderer>();
            if (spriteRenderer != null)
                spriteRenderer.color = Tint;
        }

        public void SetFrozen(bool frozen)
        {
            if (body == null)
                body = GetComponent<Rigidbody2D>();

            if (frozen == IsFrozen)
                return;

            if (frozen)
            {
                previousBodyType = body.bodyType;
                body.velocity = Vector2.zero;
                body.angularVelocity = 0f;
                body.bodyType = RigidbodyType2D.Static;
            }
            else
            {
                body.bodyType = previousBodyType == RigidbodyType2D.Dynamic
                    ? RigidbodyType2D.Dynamic
                    : RigidbodyType2D.Dynamic;
            }

            IsFrozen = frozen;
        }

        public SandboxBody CreateDuplicate(Vector2 position)
        {
            var copy = Instantiate(gameObject, position, transform.rotation);
            var copyBody = copy.GetComponent<SandboxBody>();
            copyBody.SetFrozen(false);

            foreach (var joint in copy.GetComponents<Joint2D>())
                Destroy(joint);

            var rigidbody = copy.GetComponent<Rigidbody2D>();
            rigidbody.bodyType = RigidbodyType2D.Dynamic;
            rigidbody.velocity = Vector2.zero;
            rigidbody.angularVelocity = 0f;
            return copyBody;
        }

        public void SetTint(Color color)
        {
            Tint = color;
            if (spriteRenderer != null)
                spriteRenderer.color = color;
        }
    }
}
