# DCAS-PolicyEngine

> ⚠️ **Notice:** This repository is one of the core modules in the **DCAS** project.
>
> **DCAS System Components**
>
> - [Lkas](https://github.com/leo9044/Lkas) - lane detection and broadcast loop
> - [DCAS-PolicyEngine](https://github.com/leo9044/DCAS-PolicyEngine) - **current repository**
> - [Vehicle-jetracer](https://github.com/leo9044/Vehicle-jetracer) - vehicle runtime and actuator-facing vehicle process
> - [rt-actuator](https://github.com/leo9044/rt-actuator) - real-time actuator HAL
> - [DCAS umbrella](https://github.com/leo9044/DCAS) - system blueprint and module index

Step B(상태 전이) + Step C(제어 정책) 기준선 구현을 위한 C++ 레퍼지토리입니다.

## 구조

- `include/dcas_policy_engine/types.hpp`: 공통 enum/유틸
- `include/dcas_policy_engine/step_b.hpp`, `src/step_b.cpp`: Step B 상태 전이
- `include/dcas_policy_engine/step_c.hpp`, `src/step_c.cpp`: Step C 정책 산출
- `src/main.cpp`: 간단 실행 예제 러너
- `tests/test_policy.cpp`: 핵심 정책 시나리오 테스트

## 빌드

```bash
cmake -S . -B build
cmake --build build
```

## 테스트

```bash
ctest --test-dir build --output-on-failure
```

## 러너 실행

```bash
./build/dcas_policy_runner
```

## 현재 반영 정책(요약)

- Step B:
	- `unresponsive` / `intoxicated` 감지 시 즉시 `ABSENT`
	- `ABSENT` 도달 시 run cycle 래치 유지
	- 비-critical 경로에서만 `recover_hold` 기반 `OK` 복귀 허용
- Step C:
	- `driver_state=ABSENT` 시 즉시 `MRM`, `throttle_limit=0.0`
	- `ABSENT + intoxicated`면 `driver_override_lock=true`
	- `ABSENT + unresponsive`면 `driver_override_lock=false`