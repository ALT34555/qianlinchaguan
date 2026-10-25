export type V3 = [number, number, number];

const EPS = 1e-9;

export function faceNormal(ax: number, ay: number, az: number, bx: number, by: number, bz: number, cx: number, cy: number, cz: number): V3 {
  const ux = bx - ax, uy = by - ay, uz = bz - az;
  const vx = cx - ax, vy = cy - ay, vz = cz - az;
  let nx = uy * vz - uz * vy;
  let ny = uz * vx - ux * vz;
  let nz = ux * vy - uy * vx;
  const len = Math.hypot(nx, ny, nz);
  if (len < EPS) return [0, 1, 0];
  return [nx / len, ny / len, nz / len];
}

export function rotateY(x: number, z: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [x * c - z * s, x * s + z * c];
}

export function rotateZ(x: number, y: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [x * c - y * s, x * s + y * c];
}

export function rotateX(y: number, z: number, angle: number): [number, number] {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  return [y * c - z * s, y * s + z * c];
}

export function addV3(a: V3, b: V3): V3 {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

export function subV3(a: V3, b: V3): V3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

export function scaleV3(a: V3, k: number): V3 {
  return [a[0] * k, a[1] * k, a[2] * k];
}

export function normV3(a: V3): V3 {
  const len = Math.hypot(a[0], a[1], a[2]);
  if (len < EPS) return [0, 1, 0];
  return [a[0] / len, a[1] / len, a[2] / len];
}

export function crossV3(a: V3, b: V3): V3 {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

export function lerpV3(a: V3, b: V3, t: number): V3 {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}
