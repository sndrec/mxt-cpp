export function hexToBytes(value: string): Uint8Array {
  if (!/^[0-9a-f]+$/.test(value) || (value.length & 1) !== 0) {
    throw new Error("invalid_hex");
  }
  const output = new Uint8Array(value.length / 2);
  for (let index = 0; index < output.length; ++index) {
    output[index] = Number.parseInt(value.slice(index * 2, index * 2 + 2), 16);
  }
  return output;
}

export function bytesToHex(value: ArrayBuffer | ArrayBufferView): string {
  const bytes = value instanceof ArrayBuffer
    ? new Uint8Array(value)
    : new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  let output = "";
  for (const byte of bytes) output += byte.toString(16).padStart(2, "0");
  return output;
}

async function hmacKey(secret: string, usage: ("sign" | "verify")[]): Promise<CryptoKey> {
  if (secret.length < 32) throw new Error("service_secret_too_short");
  return crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    usage,
  );
}

export async function verifyHmac(secret: string, message: string, signatureHex: string): Promise<boolean> {
  if (!/^[0-9a-f]{64}$/.test(signatureHex)) return false;
  const key = await hmacKey(secret, ["verify"]);
  return crypto.subtle.verify(
    "HMAC",
    key,
    hexToBytes(signatureHex),
    new TextEncoder().encode(message),
  );
}

export async function signHmac(secret: string, message: string): Promise<string> {
  const key = await hmacKey(secret, ["sign"]);
  return bytesToHex(await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(message)));
}

export async function deterministicRunId(steamId: string, replaySha256: string): Promise<string> {
  const digest = await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(`${steamId}\n${replaySha256}`),
  );
  return bytesToHex(digest);
}
