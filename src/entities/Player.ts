/** 玩家（观景模式） */
import type { Input } from '../core/Input';
import { WORLD_MAX_Y, WORLD_MIN_Y } from '../core/config';
import type { World } from '../systems/world/World';
import { movementScale } from './PlayerProfile';

const HALF_WIDTH = 0.3;

const GRAVITY = 32;
const JUMP_VELOCITY = 9;
const TERMINAL_VELOCITY = 78;
export const WALK_SPEED = 4.3;
export const SPRINT_SPEED = 5.6;
export const SWIM_SPEED = 2.6;
const FLY_SPEED = 12;
const FLY_SPRINT_SPEED = 36;
const FLY_VERTICAL_SPEED = 10;
const DOUBLE_TAP_MS = 300;
const MOUSE_SENSITIVITY = 0.0022;


export class Player {
  readonly ren = '人';
  ren_ming = '赤';
  private profileHeight = 170;
  private bodyHeight = 1.7;
  private speedScale = 1;
  private stride = .7;
  private stepHeight = 1;
  private collisionStep = .425;
  get heightCm(): number { return this.profileHeight; }
  set heightCm(value: number) {
    if (value === this.profileHeight) return;
    this.profileHeight = value;
    this.bodyHeight = value / 100;
    this.speedScale = movementScale(value);
    this.stride = .7 * this.speedScale;
    this.stepHeight = this.speedScale;
    this.collisionStep = Math.min(.45, this.bodyHeight * .25);
  }
  get strideLength(): number { return this.stride; }
  private crouchOffset = 0;
  get height(): number { return this.bodyHeight; }
  get crouching(): boolean { return !this.flying && this.input.isActionDown('crouch'); }
  /** 脚底中心位置 */
  x = 0;
  y = 0;
  z = 0;
  vx = 0;
  vy = 0;
  vz = 0;
  yaw = 0;
  pitch = 0;
  onGround = false;
  flying = false;
  flightAllowed = true;
  inWater = false;
  blockedForward = false;
  plantContacts = 0;
  /** 自动上台阶时的镜头平滑偏移 */
  private stepOffset = 0;
  private lastSpaceTap = -Infinity;

  constructor(private readonly world: World, private readonly input: Input) {
    input.onAction('jump', (e) => this.onSpaceTap(e.timeStamp));
  }

  spawnAt(x: number, z: number): void {
    const safe=this.world.resolveFlagSpawn?.(x,z,this.height,HALF_WIDTH);if(safe){x=safe.x;z=safe.z;}
    this.x = x;
    this.z = z;
    this.y = this.world.getGroundUnder(x,z,HALF_WIDTH) + 0.01;
    this.vx = this.vy = this.vz = 0;
  }

  get eyeY(): number {
    return this.y + this.height * .94 + this.stepOffset - this.crouchOffset;
  }

  resetInputGestures(): void { this.lastSpaceTap = -Infinity; }

  private onSpaceTap(t: number): void {
    if (!this.flightAllowed) return;
    if (t - this.lastSpaceTap <= DOUBLE_TAP_MS) {
      this.flying = !this.flying;
      this.vy = 0;
      this.lastSpaceTap = -Infinity;
    } else {
      this.lastSpaceTap = t;
    }
  }

  update(dt: number): void {
    this.blockedForward = false;
    if (!this.flightAllowed) this.flying = false;
    const resolved=this.world.resolveIceOverlap?.(this.x,this.y,this.z,this.height,HALF_WIDTH);
    if(resolved!==undefined&&resolved!==this.y){this.y=resolved;this.vy=0;}
    const input = this.input;
    const crouchTarget = this.crouching ? this.height * .35 : 0;
    this.crouchOffset += (crouchTarget - this.crouchOffset) * Math.min(1, dt * 16);

    // ---- 视角
    const [mdx, mdy] = input.consumeMouse();
    this.yaw -= mdx * MOUSE_SENSITIVITY;
    this.pitch -= mdy * MOUSE_SENSITIVITY;
    const lim = Math.PI / 2 - 0.01;
    this.pitch = Math.max(-lim, Math.min(lim, this.pitch));

    // ---- 水平移动意图
    const f = (input.isActionDown('forward') ? 1 : 0) - (input.isActionDown('back') ? 1 : 0);
    const s = (input.isActionDown('right') ? 1 : 0) - (input.isActionDown('left') ? 1 : 0);
    const sin = Math.sin(this.yaw);
    const cos = Math.cos(this.yaw);
    let wx = -sin * f + cos * s;
    let wz = -cos * f - sin * s;
    const len = Math.hypot(wx, wz);
    if (len > 0) {
      wx /= len;
      wz /= len;
    }
    const sprint = input.isActionDown('sprint');
    this.inWater = this.checkInWater();

    const brushSpeed = this.flying ? 1 : this.world.decorationSpeedFactor?.(this.x, this.y, this.z, this.height, HALF_WIDTH) ?? 1;
    const speed = (this.flying
      ? sprint ? FLY_SPRINT_SPEED : FLY_SPEED
      : this.inWater ? SWIM_SPEED : this.crouching ? WALK_SPEED * .5 : sprint ? SPRINT_SPEED : WALK_SPEED) * this.speedScale * brushSpeed;
    const accel = this.flying || this.onGround || this.inWater ? 14 : 4;
    const k = Math.min(1, accel * dt);
    this.vx += (wx * speed - this.vx) * k;
    this.vz += (wz * speed - this.vz) * k;

    // ---- 竖直
    const space = input.isActionDown('jump');
    if (this.flying) {
      const target = ((space ? 1 : 0) - (input.isActionDown('crouch') ? 1 : 0)) * FLY_VERTICAL_SPEED * (sprint ? 2 : 1) * this.speedScale;
      this.vy += (target - this.vy) * Math.min(1, 12 * dt);
    } else if (this.inWater) {
      this.vy -= GRAVITY * 0.25 * dt;
      this.vy *= Math.max(0, 1 - 3 * dt);
      if (space) this.vy = Math.min(4, this.vy + 24 * dt);
      this.vy = Math.max(this.vy, -4);
    } else {
      this.vy = Math.max(-TERMINAL_VELOCITY, this.vy - GRAVITY * dt);
      if (space && this.onGround) this.vy = JUMP_VELOCITY;
    }

    this.move(this.vx * dt, this.vy * dt, this.vz * dt);
    this.plantContacts = this.world.plantContactCount?.(this.x, this.y, this.z, this.height, HALF_WIDTH) ?? 0;

    // 镜头上台阶平滑
    this.stepOffset *= Math.exp(-14 * dt);
    if (Math.abs(this.stepOffset) < 1e-3) this.stepOffset = 0;
  }

  private checkInWater(): boolean {
    const probeY = this.y + 0.4;
    return probeY < this.world.getWaterLevel(this.x, this.z) && (this.world.getBedHeight?.(this.x,this.z)??this.world.getHeight(this.x,this.z)) <= probeY;
  }

  // 碰撞

  /** 脚底矩形与可见三角面的最高交点。 */
  private maxGroundUnder(x: number, z: number): number {
    return this.world.getGroundUnder(x, z, HALF_WIDTH,this.y);
  }

  private move(dx: number, dy: number, dz: number): void {
    const steps = Math.max(1, Math.ceil(Math.max(Math.abs(dx), Math.abs(dy), Math.abs(dz)) / this.collisionStep));
    dx /= steps;
    dy /= steps;
    dz /= steps;
    const wasGrounded = this.onGround;
    this.onGround = false;

    for (let i = 0; i < steps; i++) {
      // Y
      let ny = this.y + dy;
      const ceiling=this.world.getIceCeiling?.(this.x,this.z,HALF_WIDTH,this.y+this.height)??Infinity;
      if(dy>0&&ny+this.height>ceiling){ny=ceiling-this.height;this.vy=0;}
      const ground = Math.max(this.maxGroundUnder(this.x, this.z), dy <= 0 ? this.world.decorationLandingHeight?.(this.x, this.z, this.y, ny, HALF_WIDTH) ?? -Infinity : -Infinity);
      if (dy > 0 && this.world.isDecorationBlocked?.(this.x, ny, this.z, this.height, HALF_WIDTH)
        && !this.world.isDecorationBlocked(this.x, this.y, this.z, this.height, HALF_WIDTH)) { this.vy = 0; }
      else if (dy <= 0 && ny < ground) {
        this.y = ground;
        this.vy = 0;
        this.onGround = true;
        if (this.flying) this.flying = false; // 飞行时落地自动退出飞行
      } else {
        this.y = ny;
      }

      // X / Z
      this.moveHorizontal(dx, 0, wasGrounded);
      this.moveHorizontal(0, dz, wasGrounded);
    }

    if (this.y < WORLD_MIN_Y) {
      this.y = WORLD_MIN_Y;
      this.vy = 0;
    }
    if (this.y > WORLD_MAX_Y - this.height) {
      this.y = WORLD_MAX_Y - this.height;
      this.vy = Math.min(this.vy, 0);
    }
  }

  private moveHorizontal(dx: number, dz: number, wasGrounded: boolean): void {
    if (dx === 0 && dz === 0) return;
    const nx = this.x + dx;
    const nz = this.z + dz;
    if(!this.world.hasGroundAround(nx,nz)){this.markForwardBlocked(dx,dz);return;}
    let ground = this.maxGroundUnder(nx, nz);
    if ((wasGrounded || this.onGround) && !this.flying && this.vy <= 0) {
      ground = Math.max(ground, this.world.decorationLandingHeight?.(nx, nz, this.y + this.stepHeight, this.y - this.stepHeight - 1e-5, HALF_WIDTH) ?? -Infinity);
    }
    if (this.world.isDecorationBlocked?.(nx, Math.max(this.y, ground), nz, this.height, HALF_WIDTH)
      && !this.world.isDecorationBlocked(this.x, this.y, this.z, this.height, HALF_WIDTH)) {
      if (dx !== 0) this.vx = 0;
      if (dz !== 0) this.vz = 0;
      this.markForwardBlocked(dx, dz);
      return;
    }
    if (this.y >= ground) {
      this.x = nx;
      this.z = nz;
      // 缓坡下行贴地，跳跃飞行保持竖直运动
      if ((wasGrounded || this.onGround) && !this.flying && this.vy <= 0 && this.y - ground <= this.stepHeight) {
        this.y = ground;
        this.onGround = true;
      }
      return;
    }
    // 观景模式自动上台阶
    const rise = ground - this.y;
    if ((wasGrounded || this.onGround || this.inWater || this.flying) && rise <= this.stepHeight) {
      this.x = nx;
      this.z = nz;
      this.y = ground;
      this.stepOffset -= rise;
      return;
    }
    if (dx !== 0) this.vx = 0;
    if (dz !== 0) this.vz = 0;
    this.markForwardBlocked(dx, dz);
  }

  private markForwardBlocked(dx: number, dz: number): void {
    if (this.input.isActionDown('forward') && !this.input.isActionDown('back') && -Math.sin(this.yaw) * dx - Math.cos(this.yaw) * dz > 1e-6) this.blockedForward = true;
  }
}
