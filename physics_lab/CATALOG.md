# wuli → ESP32 实验迁移目录

基于 `liziwuli-local/pages` 的 57 个实验。

## 已移植（v0.16.0）

| # | 实验 | 状态 |
|---|---|---|
| 1 | 平抛运动 | 可玩 |
| 2 | 弹簧振子 | 可玩，含压缩/伸长 |
| 3 | 自由落体 | 可玩，真实米尺/v/a |
| 4 | 匀速直线运动 | 可玩 |
| 5 | 匀加速直线运动 | 可玩 |
| 6 | 单摆 | 可玩 |
| 7 | 力的合成 | 可玩 |

## 第二批：优先移植（2D 力学/图线）

- static-kinetic-friction 摩擦力
- conveyor-belt 传送带
- elastic-inelastic-collision 碰撞
- momentum-conservation 动量守恒
- two-dimensional-collision 二维碰撞
- mechanical-energy-conservation 机械能守恒
- potential-energy 势能
- impulse-momentum 冲量
- kinetic-energy-theorem 动能定理
- motion-composition 运动合成
- centripetal-force 向心力
- apparent-weight 视重
- locomotive-startup 机车启动
- hookes-law 胡克定律

## 第三批：电路/光学/热学/波（示意 + 图线）

- ohms-law 欧姆定律
- series-parallel-circuit 串并联
- closed-circuit-emf 闭合电路
- lens-imaging 凸透镜成像
- refraction-total-internal-reflection 折射
- ideal-gas-law 理想气体
- isothermal-process 等温过程
- mechanical-wave-formation 机械波
- wave-interference 波的干涉
- alternating-current-generation 交流电
- parallel-plate-capacitor 平行板电容

## 第四批：场/近代物理（需要图线抽象）

- coulomb-vector 库仑力
- electric-field-equipotential 电场等势线
- ampere-force-direction 安培力
- magnetic-field 磁场
- faraday-law / lenz-law 电磁感应
- photoelectric-effect 光电效应
- hydrogen-spectrum 氢光谱
- double-slit-interference / single-slit-diffraction 干涉/衍射
- radioactive-decay 衰变
- brownian-motion 布朗运动

## 不建议直接移植

- cyclotron 回旋加速器
- mass-spectrometer 质谱仪
- charged-particle-* 三维场运动
- gravitation-celestial-motion 天体运动
- nuclear-structure-model 核结构
- molecular-speed-distribution 分子速度分布
- oscilloscope-tube 示波管（3D/复杂仪表）

原因：原页面依赖 3D、复杂 DOM/SVG、会员资源或大图；在 400×300 1bpp 上只能保留公式/曲线，失去“看见物理”的核心。
