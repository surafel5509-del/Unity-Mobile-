# Offline save / score / unlock template — design, NOT implementation

See [docs/08-data-and-services.md](../docs/08-data-and-services.md) for security caveats.

Data layout proposal (JSON configuration, binary saves):

```json
{"version":1,"slots":["autosave","slot1","slot2"],
 "highScoreTable":"local_scores","unlockVerifier":"ed25519-public-key",
 "defaultNetworkAccess":false,"storage":"app-private"}
```

- Save: Android Keystore AES-256-GCM, unique nonce, atomic replace, version migration.
- Score: local SQLite transaction with game ID + score + timestamp.
- Offline unlock: locally verify signed entitlement; public key in APK, private key **never** in APK.
- Offline unlock codes are not a payment processor, and portable codes can be shared.

These APIs do not yet exist in the runtime. Do not store real purchases with this template.
