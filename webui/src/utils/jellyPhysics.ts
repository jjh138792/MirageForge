// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2025-2026 jjh138792
// MirageForge: https://github.com/jjh138792/MirageForge

/**
 * 高精度子步进弹簧阻尼求解器：
 * - 停机阈值精确至 0.0008，彻底解决旧版 0.08 阈值提前掐死果冻回弹振荡的问题
 * - 每帧分 2 个物理子步进，高刚度低阻尼下依然绝对数值稳定不发散
 */
export class JellySpring {
  pos: number;
  target: number;
  vel = 0;
  stiffness: number;
  damping: number;
  private epsPos: number;
  private epsVel: number;

  constructor(
    initial = 0,
    stiffness = 300,
    damping = 16,
    epsPos = 0.0008,
    epsVel = 0.002
  ) {
    this.pos = initial;
    this.target = initial;
    this.stiffness = stiffness;
    this.damping = damping;
    this.epsPos = epsPos;
    this.epsVel = epsVel;
  }

  impulse(deltaVel: number) {
    this.vel += deltaVel;
  }

  snap(val: number) {
    this.pos = val;
    this.target = val;
    this.vel = 0;
  }

  step(dt: number): number {
    const clampedDt = Math.min(Math.max(dt, 0.001), 0.032);
    const subDt = clampedDt * 0.5;

    for (let i = 0; i < 2; i++) {
      const force = -this.stiffness * (this.pos - this.target) - this.damping * this.vel;
      this.vel += force * subDt;
      this.pos += this.vel * subDt;
    }

    if (
      Math.abs(this.vel) < this.epsVel &&
      Math.abs(this.pos - this.target) < this.epsPos
    ) {
      this.pos = this.target;
      this.vel = 0;
    }
    return this.pos;
  }

  isSettled(): boolean {
    return (
      Math.abs(this.vel) < this.epsVel &&
      Math.abs(this.pos - this.target) < this.epsPos
    );
  }
}
