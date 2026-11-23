// A small JSON Schema checker: the subset the SDK's own schemas use (type, const, enum, required, properties,
// additionalProperties, items, minItems/maxItems, minLength/maxLength, pattern, minimum/maximum, $ref to "#/$defs/x").
// Used by the tests and by `espos ai test` to check a provider's answers against assistant-v1.schema.json; no
// network, no dependencies. Returns the problems as "path: what" lines ([] when valid).

type Schema = Record<string, any>;

const typeOf = (v: unknown): string => {
  if (v === null) return "null";
  if (Array.isArray(v)) return "array";
  if (typeof v === "number") return Number.isInteger(v) ? "integer" : "number";
  return typeof v;
};

const typeOk = (want: string, got: string) => want === got || (want === "number" && got === "integer");

export function checkSchema(schema: Schema, value: unknown, root: Schema = schema, at = "$"): string[] {
  const out: string[] = [];
  if (schema.$ref) {
    const m = /^#\/\$defs\/(.+)$/.exec(schema.$ref);
    const target = m ? root.$defs?.[m[1]] : undefined;
    if (!target) return [`${at}: unknown $ref ${schema.$ref}`];
    return checkSchema(target, value, root, at);
  }
  const t = typeOf(value);
  if (schema.type !== undefined) {
    const types: string[] = Array.isArray(schema.type) ? schema.type : [schema.type];
    if (!types.some((w) => typeOk(w, t))) return [`${at}: is ${t}, want ${types.join(" or ")}`];
  }
  if ("const" in schema && JSON.stringify(schema.const) !== JSON.stringify(value)) out.push(`${at}: must be ${JSON.stringify(schema.const)}`);
  if (schema.enum && !schema.enum.some((e: unknown) => JSON.stringify(e) === JSON.stringify(value))) {
    out.push(`${at}: ${JSON.stringify(value)} is not one of ${schema.enum.map((e: unknown) => JSON.stringify(e)).join(", ")}`);
  }
  if (t === "string") {
    const s = value as string;
    const n = [...s].length;
    if (schema.minLength !== undefined && n < schema.minLength) out.push(`${at}: shorter than ${schema.minLength}`);
    if (schema.maxLength !== undefined && n > schema.maxLength) out.push(`${at}: longer than ${schema.maxLength}`);
    if (schema.pattern && !new RegExp(schema.pattern, "u").test(s)) out.push(`${at}: does not match ${schema.pattern}`);
  }
  if (t === "number" || t === "integer") {
    const x = value as number;
    if (schema.minimum !== undefined && x < schema.minimum) out.push(`${at}: below ${schema.minimum}`);
    if (schema.maximum !== undefined && x > schema.maximum) out.push(`${at}: above ${schema.maximum}`);
  }
  if (t === "array") {
    const a = value as unknown[];
    if (schema.minItems !== undefined && a.length < schema.minItems) out.push(`${at}: fewer than ${schema.minItems} items`);
    if (schema.maxItems !== undefined && a.length > schema.maxItems) out.push(`${at}: more than ${schema.maxItems} items`);
    if (schema.items) a.forEach((x, i) => out.push(...checkSchema(schema.items, x, root, `${at}[${i}]`)));
  }
  if (t === "object") {
    const o = value as Record<string, unknown>;
    for (const k of schema.required ?? []) if (!(k in o)) out.push(`${at}: missing ${k}`);
    const props: Schema = schema.properties ?? {};
    for (const [k, v] of Object.entries(o)) {
      if (props[k]) out.push(...checkSchema(props[k], v, root, `${at}.${k}`));
      else if (schema.additionalProperties === false) out.push(`${at}: unexpected ${k}`);
      else if (schema.additionalProperties && typeof schema.additionalProperties === "object") {
        out.push(...checkSchema(schema.additionalProperties, v, root, `${at}.${k}`));
      }
    }
  }
  return out;
}

/** The problems of value against root's $defs entry name. */
export function checkDef(root: Schema, name: string, value: unknown): string[] {
  return checkSchema({ $ref: `#/$defs/${name}` }, value, root);
}
