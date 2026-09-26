# DLSS5NR-CostScaler

<p align="center">
  <a href="README-CN.md">简体中文</a> ｜ <a href="README.md">English</a> ｜ <a href="README-RU.md">Русский</a> ｜ <b>한국어</b>
</p>

> ✅ **실사 테스트 통과**: ReShade (RenoDX 계열 플러그인 포함) · 엘더스크롤 [Community Shaders](https://github.com/doodlegabe/CommunityShaders) · clshortfuse DLSS 플러그인 (`renodx-dlss.addon64`)

---

## ✨ 이 포크의 강점 (업스트림 대비)

이 저장소는 [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler)의 강화 포크입니다(업스트림 v1.0.5 완전 동기화). 업스트림은 ini 수동 수정 + 게임 재시작 방식의 순수 알고리즘 프록시이지만, 이 포크는 이를 **실시간 WYSIWYG 튜닝 툴체인**으로 격상시키고 프레임 교차 노이즈 감소를 깊게 개선했습니다:

| 기능 | 업스트림 | 이 포크 |
| --- | :-: | :-: |
| 튜닝 방식 | ini 수동 수정 + 재시작 | **게임 내 오버레이 패널 / 콘솔 / 매니저 — 변경 즉시 적용** |
| 다중 클라이언트 실시간 연동 | 없음 | 패널 ⇄ 콘솔 ⇄ 공유 메모리 ⇄ 프록시, 밀리초 단위 동기화 |
| 그래픽 설치 / 제거 | 수동 이름 변경 및 복사 | **매니저가 게임 라이브러리를 재귀 스캔, 원클릭 설치 / 제거 / 복원** |
| VRNR 프레임 스킵 | 고정 교차 프레임 | **시각적 스위치 + 전체 깜빡임 방지 체인** (0.6.3 기본 3중 → 0.7.1 가중치 하한 / MV 재투영 / 실루엣 보호 → 0.7.3 FPS 적응 강도) |
| FPS Governor | 없음 | **목표 FPS 유지를 위한 자동 해상도 단계 조정**, Frame-Gen 배율 지원 (0.7.0, 업스트림 동기화) |
| UI | — | M3E 스타일, 라이트·다크 테마, 中 / EN / RU / 한 |
| 안전성 | — | SEH 크래시 실드, 셰이더 예외가 게임을 끌어내리지 않음 |

## 🆕 새로운 기능

### 3개 클라이언트 실시간 연동 튜닝

- **게임 내 오버레이 패널** (`Ctrl+Alt+F11`): 최상위 표시, 드래그 이동, 자유 크기 조절; 포커스를 가로채지 않고 게임 입력 파이프라인을 건드리지 않음 (저수준 마우스 훅 관찰자 방식) — 패널을 닫는 즉시 게임 입력 복구;
- **독립 콘솔 `dlssnr_console.exe`**: 게임 프로세스 외부에서 실행, 공유 메모리를 통한 실시간 양방향 동기화;
- **ReShade 컴패니언 플러그인** (`dlssnr-companion.addon64`): ReShade Home 메뉴 안의 설정 페이지.

어디서 수정하든 (ini 핫 리로드 포함) 나머지 모든 곳에 즉시 반영됩니다.

### 그래픽 매니저 `DLSS5NR-CostScaler-Manager.exe`

순수 Win32 + GDI+ 자체 렌더링 Material 3 Expressive UI (런타임 의존성 없음), 4페이지: **빠른 설치** (라이브러리 재귀 스캔, 원클릭 설치 / 제거 / 복원) · **설치됨** · **기록** · **패널 디버그** (프록시 ini 직접 읽기/쓰기, 500ms 디바운스 자동 저장).

### FPS Governor (0.7.0, 업스트림 동기화)

동적 해상도 단계 상태 머신: 프레임 시간을 EWMA로 평활화(~30프레임 창)하여 목표 FPS와 비교 — 목표의 95% 미만이 1.0초 지속되면 5% 하향, 115% 초과가 3.0초 지속되면 5% 상향, 각 조정 후 쿨다운 대기(기본 2.0초). 단계 전환은 멀티슬롯 파이프라인 캐시로 대기 시간 0ms 즉시 전환; **프레임 생성(FG) 모드**에서는 "표시 FPS = 베이스 × 배율" 기준으로 목표를 판정하여 DLSS 3 / FSR 3 / Lossless Scaling 2x/3x/4x에 대응합니다.

### 스킵 프레임 깜빡임 방지 체인 (0.6.3 → 0.7.1 → 0.7.3, 이 포크 전용)

교차 프레임 VRNR을 켜면 추론 프레임(풀 강도 신경 재구성)과 스킵 프레임(오래된 edit × 감쇠 가중치)이 화면에서 번갈아 표시됩니다 — FPS가 낮고 프레임당 이동이 클수록 눈에 민감한 주파수 대역의 명암 펄스가 커집니다. 이 포크의 누적 수정 사항 (마스터 스위치 `VrnrAntiFlicker`, 기본 켜짐):

- **시간 램프 (0.6.3, 0.7.1에서 수정)**: 연속 2프레임 이상 스킵 시 잔차 강도가 100% → 90% → 72% → 55%로 완만히 감소; 교차 패턴(각 스킵 프레임의 연속 카운트는 항상 1)은 더 이상 잘못 할인되지 않아 10% 주기 펄스 제거;
- **공간 스무딩 (0.6.3)**: 페이드아웃 가중치를 3×3 이웃 평균 휘도 차로 계산하여, 원본 노이즈가 더 이상 프레임마다 가중치를 흔들지 않음;
- **대칭 하이라이트 보호 (0.6.3)**: 스킵 프레임에도 동일한 HDR 하이라이트 클램프를 적용하여 두 경로가 밝은 영역에서 일치;
- **가중치 하한 (0.7.1, 실험)**: 스킵 프레임의 edit 가중치가 `VrnrWeightFloor`(기본 0.60) 아래로 떨어지지 않음 — 움직이는 영역이 0으로 붕괴 후 다음 추론 프레임에서 튀어 올라가는 현상 제거;
- **MV 재투영 (0.7.1, 실험)**: 스킵 프레임에서 게임의 모션 벡터로 2프레임 전의 캐시된 신경 입력을 현재 프레임에 정렬한 뒤 휘도 차를 계산 — "어긋난 프레임이 움직임으로 오인"되어 발생하는 전체 화면 페이드아웃 펄스 제거; 더 잘 정렬된 샘플이 이기므로 MV 부호 규약은 무관;
- **실루엣 보호 (0.7.1, 실험)**: 깊이 불연속 지점(캐릭터 / 오브젝트 윤곽)에서 스킵 프레임의 edit 가중치를 ≤0.10으로 제한하여, 오래된 edit가 윤곽을 넘어 고스팅을 만들지 않음;
- **FPS 적응 강도 (0.7.3, 실험)**: 낮은 FPS에서 **추론 프레임의** edit 강도를 적응적으로 감쇠(`VrnrAdapt`, 기본 켜짐)하여 양쪽에서 시각적 차이를 축소 — 순수 CPU 측 구현, 새로운 GPU 리소스 제로.

### 알고리즘 레이어 (업스트림 v1.0.5 동기화)

하드웨어 이중선형 다운샘플 (LDS 타일 캐시) → 저해상도 NR 추론 → **고주파 정합 잔차 합성**으로 원본 프레임에 복원; **25%–200% 슈퍼샘플링**, **비대칭(아나모픽) 스케일링** (실험), **깊이 인식 실루엣 보존**, HDR 휘도 클램프 + RCAS 샤프닝, DRS 동적 서브렉트 추적, SDR / HDR10 PQ / scRGB / R11G11B10.

---

## 이것은 무엇인가

NVIDIA DLSS-NR (DirectX 12)용 독립 프록시 DLL 및 도구 세트: 신경 재구성 모델을 더 낮은 해상도에서 추론시키면서, "고주파 정합 잔차" 셰이더로 원본 1:1 기하학, 미세 텍스처, 텍스트, 가장자리 디테일을 보존합니다 — **흐림 없이 DLSS-NR의 GPU 비용을 표시 해상도에서 분리**합니다. 아키텍처는 호스트 비종속적입니다: DirectX 12를 통해 `nvngx_dlssnr.dll`을 호출하는 모든 게임, 엔진, 인젝터에서 사용할 수 있습니다.

**작동 방식**: 원본 DLL이 `nvngx_dlssnr_real.dll`로 이름 변경되며, 프록시가 NGX 호출을 가로채 — 원본 프레임을 다운샘플하고(거의 공짜) DLSS-NR에 넘긴 뒤, 신경망의 델타를 정합 잔차 방식으로 훼손되지 않은 원본 프레임에 다시 합성합니다.

---

## 빌드 산출물

빌드 결과는 `build\<버전>\`(현재 `build/0.7.3/`)에 버전 정보가 내장되어 출력됩니다:

| 파일 | 용도 |
| --- | --- |
| `nvngx_dlssnr.dll` | 프록시 본체 (게임 폴더에 배치; 실제 `nvngx_dlssnr_real.dll`로 포워딩) |
| `DLSS5NR-CostScaler-Manager.exe` | 그래픽 매니저 (설치 / 제거 / 설치됨 / 기록 / 패널 디버그) |
| `dlssnr_console.exe` | 독립 디버그 콘솔 (게임 외부 실시간 동기화) |
| `nvngx_dlssnr.ini` | 설정 파일 (게임 내 매초 핫 리로드) |
| `dlssnr-companion.addon64` | ReShade 컴패니언 플러그인 (선택) |

---

## 빠른 시작

1. 매니저와 프록시 `nvngx_dlssnr.dll`, `nvngx_dlssnr.ini`를 같은 폴더에 놓습니다 (자동 인식);
2. 매니저 열기 → "빠른 설치" → ② 게임 폴더 또는 라이브러리 루트 선택 → 스캔 → 체크 → **설치**;
3. 게임 실행, `Ctrl+Alt+F11`로 패널 호출; 또는 매니저 "패널 디버그" 페이지에서 수정 (게임 내 1초 내 핫 리로드).

<details>
<summary>수동 설치 (동일 절차)</summary>

1. 게임 폴더에서 원본 `nvngx_dlssnr.dll`을 `nvngx_dlssnr_real.dll`로 이름 변경;
2. 프록시 `nvngx_dlssnr.dll`과 `nvngx_dlssnr.ini`를 같은 폴더에 복사;
3. *(선택)* `dlssnr_console.exe`를 함께 두면 게임 외부에서 튜닝; `dlssnr-companion.addon64`는 ReShade 메뉴용;
4. 게임 시작, `Ctrl+Alt+F11`로 패널 열기.

</details>

---

## 설정 빠른 참조 (`nvngx_dlssnr.ini`)

시작 시 읽고 이후 매초 핫 리로드됩니다. 아래는 요약표이며, **각 키의 용도와 원리는 다음 섹션 "설정 상세 해설"을 참조하세요**.

```ini
[DLSSNR_Proxy]
EnableProxy = 1              ; 1 = 프록시 활성; 0 = 네이티브 통과
ResolutionScale = 0.75       ; 추론 해상도 스케일 (0.25 ~ 2.00)
EnableAnamorphic = 0         ; 아나모픽 스케일링 (실험)
ResolutionScaleX = 0.65      ; 수평 스케일 (0.25 ~ 2.00)
ResolutionScaleY = 0.85      ; 수직 스케일 (0.25 ~ 2.00)
EnlargementMode = 1          ; 1 = 정합 잔차; 0 = 이중선형
TransferStrength = 1.00      ; 잔차 합성 강도 (0 ~ 2)
ColorStrength = 1.00         ; 색상 강도 (0 ~ 1)
Sharpness = 0.20             ; RCAS 샤프닝 (0 ~ 1)
EnableDepthAwareResolve = 1  ; 깊이 인식 실루엣 보존
EnableAlternatingFrames = 0  ; 교차 프레임 VRNR (실험, 기본 꺼짐)
VrnrAntiFlicker = 1          ; 스킵 프레임 깜빡임 방지 마스터 스위치 (기본 켜짐)
VrnrWeightFloor = 0.60       ; 스킵 프레임 edit 가중치 하한 (실험, 0 ~ 1)
VrnrReproject = 1            ; 모션 벡터 재투영 (실험)
VrnrAdapt = 1                ; FPS 적응 강도 (실험, 0.7.3)
VrnrAdaptAmount = 0.60       ; 적응 최대 감쇠 비율 (0 ~ 1)
VrnrAdaptFpsHi = 45          ; 이 FPS 아래에서 감쇠 시작
VrnrAdaptFpsLo = 25          ; 이 FPS 이하에서 최대 감쇠
EnableHotkeys = 1            ; 게임 내 단축키 마스터 스위치
EnableUi = 1                 ; 게임 내 오버레이 패널 마스터 스위치
UiLanguage = 0               ; 패널 언어: 0 中 / 1 EN / 2 RU / 3 한
PanelX/Y/W/H = ...           ; 패널 창 위치 / 크기 (자동 저장)

[DLSSNR_Settings]
UseCustomSettings = 0        ; 0 = 호출 측 NR 파라미터 통과; 1 = 아래 값으로 재정의
Style = 0                    ; 0 = 밸런스 / 1 = 샤프 / 2 = 시네마틱
Intensity = 1.00             ; 재구성 강도 (0 ~ 2)
LocalStructureStrength = 1.00  ; 로컬 구조 보존 (0 ~ 2)
LocalToneStrength = 1.00       ; 로컬 톤 (0 ~ 2)
SkinStructureStrength = -1.00  ; 피부 구조 (-1 = 자동, 0 ~ 2)
UseAutoMask = 0              ; 빠른 움직임 / 작은 요소 자동 마스크

[Hotkeys]
RequireCtrlAlt = 1           ; 단축키에 Ctrl+Alt 접두사 필요 여부
KeyToggleProxy = 32          ; 프록시 토글 (VK 코드, 32 = Space)
KeyToggleMode = 35           ; 모드 토글 (35 = End)
KeyScaleUp = 33              ; 스케일 업 (33 = PageUp)
KeyScaleDown = 34            ; 스케일 다운 (34 = PageDown)
KeyToggleUI = 122            ; 패널 토글 (122 = F11)

[Governor]
EnableGovernor = 0           ; 동적 해상도 Governor (기본 꺼짐)
TargetFps = 60.0             ; 목표 FPS 예산 (30 ~ 240)
MinScale = 0.50              ; 최소 스케일 단계
MaxScale = 1.00              ; 최대 스케일 단계
HysteresisSec = 2.0          ; 단계 전환 쿨다운 대기 (초)
EnableFgMode = 0             ; 프레임 생성 목표 모드
FgMultiplier = 2.0           ; FG 배율 (2/3/4)
```

---

## 설정 상세 해설: 각 스위치의 용도와 원리

### 프록시와 스케일링 (`[DLSSNR_Proxy]`)

| 키 | 용도 | 원리 |
| --- | --- | --- |
| `EnableProxy` | 프록시 마스터 스위치. 0 = 모든 NGX 호출이 무변경 통과, 원본 DLL과 동일한 동작 | 단축키 / 패널로 실시간 전환; 전환은 가로채기 경로의 스케일링 결정만 바꾸며 D3D 리소스를 재생성하지 않음 |
| `ResolutionScale` | 신경 추론 해상도. 0.75 ≈ 40% 추론 절감(권장 스위트 스팟); 0.50 = DLSS Performance 비율; 2.00 = 200% 슈퍼샘플링 (DLDSR / 사진 모드) | 추론 비용은 픽셀 수에 비례합니다. 스케일은 **모델에 공급되는 내부 버퍼**에만 적용되며, 최종 이미지는 항상 전체 해상도 원본 프레임에 합성되므로 다운스케일 ≠ 디테일 손실 |
| `EnableAnamorphic` + `ResolutionScaleX/Y` | (실험) 수평 / 수직 독립 스케일이 균일 스케일을 대체 | 와이드스크린 장면에서는 수직 디테일이 더 중요; 0.65×0.85 조합은 추가로 ~45% 추론 부하를 절감하면서도 프레임 페이싱이 견고합니다. 각 축이 독립적으로 다운/업샘플됩니다 |
| `EnlargementMode` | 1 = 정합 잔차 (권장); 0 = 이중선형 직출력 + RCAS | 정합 잔차: 원본 1:1 픽셀이 앵커 — 신경 출력의 **델타(edit)**만 더해지므로 기하학, 텍스트, UI가 구조적으로 무손실. 이중선형: 신경 출력을 직접 업스케일하고 RCAS가 선명도를 구함 — 손실이 더 큼 |
| `TransferStrength` | 잔차 합성 강도 (0 ~ 2). 1.0 = 신경 델타를 그대로 전달; 0 = 순수 원본 이미지 | 잔차 = 신경 출력 − 다운샘플 입력. 계수를 곱하는 것은 델타를 스케일하는 것: 낮추면 더 "원본스럽게", 높이면(>1) 조명 변화가 과장됨 |
| `ColorStrength` | 색채 강도 (0 ~ 1). 0 = 원본 색상을 유지하고 휘도만 스케일 | 신경 델타를 휘도 방향과 색도 방향으로 분해: 1.0은 완전한 신경 색상(간접광 반사 / 재질 색) 유지, 0은 원본 픽셀의 색도 벡터를 따라 스케일된 휘도 델타만 유지하여 신경망의 색 왜곡 / 탁함 제거 (업스트림 15f09dd 색상 보존 리워크) |
| `Sharpness` | 대비 적응형 RCAS 샤프닝 (0 ~ 1) | FidelityFX RCAS: 이웃 휘도 범위에 따라 샤프닝 가중치가 적응(평탄부 약하게, 모서리 강하게)하여 오버슈트 링잉 방지. 멀티 패스 파이프라인이 동일 버퍼에 쓸 때는 자동 비활성화되어 지수적 모서리 링잉 방지 |
| `EnableDepthAwareResolve` | 깊이 인식 양방향 실루엣 보존 | 원본 깊이 버퍼의 4개 이웃을 샘플링하여 기하학적 실루엣을 감지; 불연속 지점에서 저해상도 신경 델타의 블렌드 가중치를 낮춰(최소 0.25) 저해상도 광원이 전경 모서리를 넘어 번지지 않도록 함 |

### VRNR 교차 프레임 체인 (실험)

| 키 | 용도 | 원리 |
| --- | --- | --- |
| `EnableAlternatingFrames` | 매 2번째 프레임에만 DLSS-NR 추론 실행; 중간 프레임은 이전 신경 edit를 재사용하여 평균 FPS 향상 | 추론 프레임은 전체 체인 실행; 스킵 프레임은 합성 셰이더만 실행 — 휘도 차 가중치로 감쇠된 **캐시된 오래된 edit**를 적용. 오래된 델타가 현재 프레임과 어긋날수록 가중치가 낮아짐(이미지는 원본 픽셀로 회귀). 대가: 프레임 페이싱 톱니와 휘도 펄스 → 아래 세 행 참조 |
| `VrnrAntiFlicker` | 깜빡임 방지 마스터 스위치 (0.6.3 트리플 픽스 + 0.7.1/0.7.3 개선항의 전제) | 위의 "스킵 프레임 깜빡임 방지 체인" 섹션 참조. 꺼짐 = 0.6.2와 완전히 동일한 동작 |
| `VrnrWeightFloor` | (실험) 스킵 프레임 edit 가중치 하한 (기본 0.60, 0 = 꺼짐) | 격한 움직임 → 큰 휘도 차 → 가중치 → 0 → 다음 추론 프레임이 갑자기 풀 강도 복원: "붕괴 ↔ 복원" 팝. 하한을 두면 움직이는 영역이 최소 60% 잔차를 유지하여 복원 프레임이 더 이상 튀지 않음. 높이면 안정적, 낮추면 고스팅 감소 |
| `VrnrReproject` | (실험) 모션 벡터 재투영 (기본 켜짐) | 카메라 팬 시 2프레임 전의 신경 입력이 현재 프레임과 어긋나 휘도 차가 "움직임"으로 오인되어 전체 화면이 페이드아웃. 이 스위치는 오래된 입력을 게임 MVec으로 현재 프레임에 먼저 **정렬**한 뒤 차이를 계산; 정렬이 틀리면 차이가 자연히 더 커져 더 잘 정렬된 샘플이 이김 — MV 부호 규약 무관. MVec을 사용할 수 없으면 자동 폴백 |
| `VrnrAdapt` | (실험, 0.7.3) FPS 적응 강도 마스터 스위치 | 낮은 FPS에서는 프레임당 이동이 커서 스킵 가중치가 더 많이 떨어지고, 풀 강도 추론 프레임과의 격차가 눈에 민감한 15~22Hz 대역의 휘도 펄스를 만듭니다. 이 스위치는 낮은 FPS에서 **추론 프레임을 감쇠**하여 양쪽에서 격차를 축소. 순수 CPU 측: EWMA 평활화된 FPS가 0~1 감쇠 계수를 만들어 셰이더 상수 1개로 전달 — 새로운 GPU 리소스 제로 |
| `VrnrAdaptAmount` | 최대 감쇠 비율 (0 ~ 1, 기본 0.60). 1.00 = 매우 낮은 FPS에서 추론 프레임의 신경 edit 완전 억제 | 스킵 프레임이 이미 감쇠되는 정도와 균형: 스킵은 가중치 체인으로, 추론 프레임은 이 계수로 감쇠 — 격차가 작을수록 펄스도 작아짐 |
| `VrnrAdaptFpsHi` / `VrnrAdaptFpsLo` | Smoothstep 감쇠 창 (기본 45 / 25) | EWMA FPS ≥ Hi에서는 변경 없음; Hi→Lo 구간은 부드럽게 전환(smoothstep으로 계단감 제거); ≤ Lo에서 `VrnrAdaptAmount` 최대 감쇠 도달 |

### FPS Governor (`[Governor]`)

| 키 | 용도 | 원리 |
| --- | --- | --- |
| `EnableGovernor` | 동적 해상도 단계 조정: 목표 FPS를 유지하도록 `ResolutionScale` 자동 조정 | 프레임 시간이 이상치 필터(>80ms 로딩 화면 / ≤0.5ms 폐기)를 통과한 뒤 ~30프레임 EWMA 평활화; 유효 FPS가 목표의 95% 미만 1.0초 지속 시 5% 하향, 115% 초과 3.0초 지속 시 5% 상향, 각 변경 후 진동 방지 쿨다운 대기 |
| `TargetFps` | 목표 FPS 예산 (30 ~ 240) | Governor는 `MinScale`~`MaxScale` 범위 내에서만 단계 조정; 범위 밖에서는 정지 |
| `MinScale` / `MaxScale` | 스케일 단계 경계 (0.25 ~ 2.00) | 단계는 5%씩 증감; 전환은 멀티슬롯 파이프라인 캐시로 0ms 즉시 교체(다른 스케일의 중간 버퍼가 상주) |
| `HysteresisSec` | 단계 전환 쿨다운 대기 (0.5 ~ 10.0초, 기본 2.0) | 임계값 부근에서 빠른 상/하향 전환으로 인한 해상도 "호흡" 방지 |
| `EnableFgMode` | 프레임 생성 목표 모드 | 프레임 생성 시 "표시 FPS = 베이스 렌더 FPS × 배율"입니다. 활성화하면 `TargetFps`를 **표시 FPS** 기준으로 판정하여 FG 게임 중 Governor의 잘못된 하향 방지 |
| `FgMultiplier` | FG 배율 (2.0 / 3.0 / 4.0) | DLSS 3 / FSR 3 / Lossless Scaling 2x/3x/4x에 대응; 유효 FPS = 측정 FPS × 배율 |

### NVIDIA 공식 NR 파라미터 (`[DLSSNR_Settings]`)

| 키 | 용도 | 원리 |
| --- | --- | --- |
| `UseCustomSettings` | 0 = 호출 측(게임 / OptiScaler / RenoDX)의 NR 파라미터 통과; 1 = 이 섹션으로 재정의 | 프록시는 기본적으로 모델 내부 파라미터를 건드리지 않아 호스트 동작을 유지; 개인화된 노이즈 감소 스타일이 필요할 때만 활성화 |
| `Style` | 0 = 밸런스(기본) / 1 = 샤프 / 2 = 시네마틱 | 미세 대비, 모서리 선명도, 필름적 부드러움에 각각 치우친 세 가지 공식 프리셋 |
| `Intensity` | 전체 노이즈 감소 / 재구성 강도 (0 ~ 2) | 모델의 내부 노이즈 감소 강도 파라미터를 직접 스케일 |
| `LocalStructureStrength` | 로컬 구조 보존 (0 ~ 2) | 모델이 고주파 기하학 / 텍스처 구조를 얼마나 강하게 보존하는지 |
| `LocalToneStrength` | 로컬 톤 (0 ~ 2) | 로컬 HDR 휘도 대비와 미세 전환이 얼마나 보존되는지 |
| `SkinStructureStrength` | 피부 구조 (-1 = 자동, 0 ~ 2) | -1에서 모델이 시맨틱 휴리스틱으로 피부 영역을 추론하여 텍스처를 자동 보호 |
| `UseAutoMask` | 빠른 움직임 / 작은 요소 자동 마스크 | 내부 휴리스틱 마스크: 너무 빠르거나 얇은 요소는 노이즈 감소 개입을 줄여 번짐 감소 |

### 단축키 (`[Hotkeys]`)

| 키 | 용도 | 비고 |
| --- | --- | --- |
| `RequireCtrlAlt` | Ctrl+Alt 조합 접두사 필요 여부 | 게임 키 바인딩과의 충돌 방지 |
| `KeyToggleProxy` | 프록시 토글 (기본 Space) | `EnableProxy`의 런타임 스위치 |
| `KeyToggleMode` | 정합 잔차 / 이중선형 토글 (기본 End) | `EnlargementMode`의 런타임 스위치 |
| `KeyScaleUp` / `KeyScaleDown` | 추론 해상도 올림 / 내림 (기본 PageUp / PageDown) | `ResolutionScale`의 런타임 조정 |
| `KeyToggleUI` | 게임 내 패널 토글 (기본 F11; 패널 토글은 항상 Ctrl+Alt 필요) | 패널 위치 / 크기 / 테마 / 언어는 자동 저장 |

---

## 시스템 요구 사항

- Windows 10 / 11 (64비트)
- NVIDIA RTX GPU (RTX 20 / 30 / 40 / 50 시리즈)
- DirectX 12를 통해 NVIDIA DLSS-NR(`nvngx_dlssnr.dll`)을 사용하는 게임 또는 플러그인

## 빌드 (x64, MSVC)

```text
build.bat
```

전체 파이프라인 실행: HLSL 셰이더 컴파일 (fxc) → 버전 리소스 → 프록시 DLL → 콘솔 → 매니저. 결과물은 `build\<버전>\`에, 중간 파일은 `build\obj\`에 출력; 버전은 `build.bat`(`VERSION`)과 `app_version.rc`에서 관리. 컴패니언 플러그인은 별도 빌드: `companion\build_companion.bat`.

## 자주 묻는 질문

- **게임 폴더가 Program Files에 있나요?** 매니저를 관리자 권한으로 실행하거나 수동 설치하세요.
- **패널이 마우스 입력을 받지 못하나요?** 패널은 관찰자 모드 저수준 마우스 훅을 사용하며 게임과 입력을 경쟁하지 않습니다; 안티치트가 차단하면 제보해 주세요.
- **교차 프레임 VRNR을 켤 가치가 있나요?** 네 — 깜빡임 방지 체인을 기본값(`VrnrAntiFlicker` / `VrnrWeightFloor` / `VrnrReproject` / `VrnrAdapt` 모두 켜짐)으로 두면 됩니다. 낮은 FPS에서 미세한 펄스가 남으면 `VrnrAdaptAmount`를 0.8~1.0으로 올리고, 화면이 흐릿해 보이면 낮추세요.
- **크래시 로그에 `DXGI_ERROR_DEVICE_REMOVED`가 보이나요?** GPU 드라이버 리셋(TDR)으로, 패널과 무관합니다(패널은 게임의 D3D 장치를 건드리지 않음); 오버클럭 / 드라이버 버전을 확인하세요.

## 감사의 말과 라이선스

- 업스트림 알고리즘: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (MIT; 이 저장소는 강화 포크)
- 디자인 토큰: [creeper-qt](https://github.com/creeper5820/creeper-qt) (MIT) BlueMiku 테마
- 감사 대상: [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR), [OptiScaler](https://github.com/optiscaler/OptiScaler), [clshortfuse/RenoDX](https://github.com/clshortfuse/renodx), [AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (RCAS), [Community Shaders](https://github.com/doodlegabe/CommunityShaders) 팀
