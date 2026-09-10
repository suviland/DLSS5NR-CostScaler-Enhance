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
| VRNR 프레임 스킵 | 고정 교차 프레임 | **시각적 스위치 + 스킵 프레임 깜빡임 방지** (시간 램프 / 공간 스무딩 / 대칭 하이라이트 보호) |
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

### 페이지형 패널 = 완전한 콘솔

**기본 / 고급 / NR / 단축키** 4페이지가 모든 설정을 커버: 25%–200% 스케일 칩, 전달 / 색상 / RCAS 슬라이더, 깊이 인식, 비대칭 X/Y, **NVIDIA 공식 NR 파라미터 영역**, Ctrl+Alt 조합이 가능한 시각적 단축키 리바인딩; 라이트·다크 테마, 4개 언어, 위치·크기 기억.

### 스킵 프레임 깜빡임 방지 (0.6.3, 이 포크 전용)

교차 프레임 VRNR을 켜면 추론 프레임과 스킵 프레임이 화면에서 번갈아 표시되어 30Hz 휘도 사각파와 노이즈 "호흡"이 생기기 쉽습니다. 이 포크는 3중 수정을 제공합니다 (마스터 스위치 `VrnrAntiFlicker`, 기본 켜짐):

- **시간 램프**: 연속 스킵 시 잔차 강도가 100% → 90% → 72% → 55%로 완만히 감소하고 추론 재개 시 자동 복귀 — 움직임 경계의 급격한 변화 제거;
- **공간 스무딩**: 페이드아웃 가중치를 3×3 이웃 평균 휘도 차로 계산하여, 원본 노이즈가 더 이상 프레임마다 가중치를 흔들지 않음;
- **대칭 하이라이트 보호**: 스킵 프레임에도 동일한 HDR 하이라이트 클램프를 적용하여 두 경로가 밝은 영역에서 일치.

새로운 GPU 리소스 제로, 셰이더 리소스 바인딩 무변경; 스위치를 끄면 0.6.2와 완전히 동일하게 동작.

### 알고리즘 레이어 (업스트림 v1.0.5 동기화)

하드웨어 이중선형 다운샘플 (LDS 타일 캐시) → 저해상도 NR 추론 → **고주파 정합 잔차 합성**으로 원본 프레임에 복원; **25%–200% 슈퍼샘플링**, **비대칭(아나모픽) 스케일링** (실험), **깊이 인식 실루엣 보존**, HDR 휘도 클램프 + RCAS 샤프닝, DRS 동적 서브렉트 추적, SDR / HDR10 PQ / scRGB / R11G11B10.

---

## 이것은 무엇인가

NVIDIA DLSS-NR (DirectX 12)용 독립 프록시 DLL 및 도구 세트: 신경 재구성 모델을 더 낮은 해상도에서 추론시키면서, "고주파 정합 잔차" 셰이더로 원본 1:1 기하학, 미세 텍스처, 텍스트, 가장자리 디테일을 보존합니다 — **흐림 없이 DLSS-NR의 GPU 비용을 표시 해상도에서 분리**합니다. 아키텍처는 호스트 비종속적입니다: DirectX 12를 통해 `nvngx_dlssnr.dll`을 호출하는 모든 게임, 엔진, 인젝터에서 사용할 수 있습니다.

**작동 원리**: 원본 DLL은 `nvngx_dlssnr_real.dll`로 이름이 변경되고, 프록시가 NGX 호출을 가로챕니다 — 원본 프레임을 다운샘플하고(거의 공짜) DLSS-NR에 넘긴 뒤, 네트워크의 델타를 정합 잔차 방식으로 훼손되지 않은 원본 프레임에 다시 합성합니다.

---

## 산출물

빌드 결과물은 `build\<버전>\` (현재 `build/0.6.3/`)에 버전 정보가 내장되어 출력됩니다:

| 파일 | 용도 |
| --- | --- |
| `nvngx_dlssnr.dll` | 프록시 본체 (게임 폴더에 배치, 실제 `nvngx_dlssnr_real.dll`로 포워딩) |
| `DLSS5NR-CostScaler-Manager.exe` | 그래픽 매니저 (설치 / 제거 / 설치 목록 / 기록 / 패널 디버그) |
| `dlssnr_console.exe` | 독립 디버그 콘솔 (게임 외부 실시간 동기화) |
| `nvngx_dlssnr.ini` | 설정 파일 (게임 내 매초 핫 리로드) |
| `dlssnr-companion.addon64` | ReShade 컴패니언 플러그인 (선택) |

---

## 빠른 시작

1. 매니저와 프록시 `nvngx_dlssnr.dll`, `nvngx_dlssnr.ini`를 같은 폴더에 넣습니다 (자동 인식);
2. 매니저 실행 → "빠른 설치" → ② 게임 폴더 또는 라이브러리 루트 선택 → 스캔 → 체크 → **설치**;
3. 게임 실행, `Ctrl+Alt+F11`로 패널 호출; 또는 매니저 "패널 디버그" 페이지에서 수정 (게임 내 1초 내 핫 리로드).

<details>
<summary>수동 설치 (동일 절차)</summary>

1. 게임 폴더에서 기존 `nvngx_dlssnr.dll`을 `nvngx_dlssnr_real.dll`로 이름 변경;
2. 프록시 `nvngx_dlssnr.dll`과 `nvngx_dlssnr.ini`를 같은 폴더에 복사;
3. *(선택)* `dlssnr_console.exe`를 함께 넣으면 게임 밖에서 튜닝; `dlssnr-companion.addon64`를 넣으면 ReShade 메뉴에서 조정;
4. 게임 시작, `Ctrl+Alt+F11`로 패널 열기.

</details>

---

## 설정 (`nvngx_dlssnr.ini`)

시작 시 읽고, 이후 매초 자동 핫 리로드:

```ini
[DLSSNR_Proxy]
EnableProxy = 1          ; 1 = 프록시 활성; 0 = 원본 통과
ResolutionScale = 0.75   ; 추론 해상도 스케일 (0.25 ~ 2.00; 2.00 = 200% 슈퍼샘플링)
EnableAnamorphic = 0     ; 비대칭 스케일링 (실험): 아래 X/Y 독립 스케일 활성화
ResolutionScaleX = 0.65  ; 가로 스케일 (0.25 ~ 2.00)
ResolutionScaleY = 0.85  ; 세로 스케일 (0.25 ~ 2.00)
EnlargementMode = 1      ; 1 = 정합 잔차 (Matched); 0 = 이중선형 (Bilinear)
TransferStrength = 1.00  ; 잔차 합성 강도 (0 ~ 2)
ColorStrength = 1.00     ; 색채 강도 (0 ~ 1)
Sharpness = 0.20         ; RCAS 샤프닝 (0 ~ 1)
EnableDepthAwareResolve = 1 ; 깊이 인식 실루엣 보존
EnableAlternatingFrames = 0 ; 교차 프레임 VRNR (실험, 기본 꺼짐)
VrnrAntiFlicker = 1        ; 스킵 프레임 깜빡임 방지 (0.6.3, 기본 켜짐; 끄면 0.6.2와 동일)
EnableHotkeys = 1        ; 게임 내 단축키 마스터 스위치
EnableUi = 1             ; 게임 내 패널 마스터 스위치
UiLanguage = 0           ; 패널 언어: 0 中 / 1 EN / 2 RU / 3 한
PanelX = 1500            ; 패널 창 위치 / 크기 (자동 저장, 콘솔도 읽고 씀)
PanelY = 120
PanelW = 396
PanelH = 640

[DLSSNR_Settings]
UseCustomSettings = 0    ; 0 = 호출자의 NR 파라미터 통과; 1 = 아래 값으로 재정의
Style = 0                ; 0 = 밸런스 / 1 = 샤프 / 2 = 시네마틱
Intensity = 1.00         ; 재구성 강도 (0 ~ 2)
LocalStructureStrength = 1.00  ; 로컬 구조 보존 (0 ~ 2)
LocalToneStrength = 1.00       ; 로컬 톤 (0 ~ 2)
SkinStructureStrength = -1.00  ; 피부 구조 (-1 = 자동, 0 ~ 2)
UseAutoMask = 0          ; 빠른 움직임/작은 요소 자동 마스크

[Hotkeys]
RequireCtrlAlt = 1       ; 단축키에 Ctrl+Alt 접두사 필요 여부
KeyToggleProxy = 32      ; 프록시 전환 (VK 코드)
KeyToggleMode = 35       ; 모드 전환
KeyScaleUp = 33          ; 스케일 증가
KeyScaleDown = 34        ; 스케일 감소
KeyToggleUI = 123        ; 패널 호출 (기본 F12)
```

> 위의 모든 설정은 매니저 "패널 디버그" 페이지나 게임 내 패널에서 시각적으로 수정할 수 있으며, 변경 사항은 자동 저장됩니다

---

## 시스템 요구 사항

- Windows 10 / 11 (64비트)
- NVIDIA RTX 그래픽 카드 (RTX 20 / 30 / 40 / 50 시리즈)
- DirectX 12를 통해 NVIDIA DLSS-NR (`nvngx_dlssnr.dll`)을 사용하는 게임 또는 플러그인

## 빌드 (x64, MSVC)

```text
build.bat
```

전체 파이프라인 자동 실행: HLSL 셰이더 컴파일 (fxc) → 버전 리소스 → 프록시 DLL → 콘솔 → 매니저. 결과물은 `build\<버전>\`에, 중간 파일은 `build\obj\`에 출력; 버전은 `build.bat` (`VERSION`)과 `app_version.rc`에서 관리합니다.

## 자주 묻는 질문

- **게임 폴더가 Program Files에 있나요?** 매니저를 관리자 권한으로 실행하거나 수동으로 설치하세요.
- **패널이 마우스를 받지 못하나요?** 패널은 관찰자 모드의 저수준 마우스 훅을 사용하며 게임과 입력을 경쟁하지 않습니다; 안티치트에 차단되면 보고해 주세요.
- **교차 프레임 VRNR을 켤 만한가요?** **스킵 프레임 깜빡임 방지**(`VrnrAntiFlicker`, 기본 켜짐)와 함께 쓰면 베어 프레임 스킵보다 훨씬 좋습니다: 노이즈 호흡과 움직임 경계의 팝핑이 모두 완화됩니다. 특정 게임에서 여전히 만족스럽지 않으면 `EnableAlternatingFrames = 0`을 유지하세요.
- **크래시 로그에 `DXGI_ERROR_DEVICE_REMOVED`가 보이나요?** GPU 드라이버 리셋(TDR)이며 패널과 무관합니다 (패널은 게임의 D3D 장치를 건드리지 않음); 오버클럭 / 드라이버 버전을 확인하세요.

## 감사의 말 및 라이선스

- 업스트림 알고리즘: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (MIT; 이 저장소는 강화 포크)
- 디자인 토큰 참조: [creeper-qt](https://github.com/creeper5820/creeper-qt) (MIT) BlueMiku 테마
- 감사 대상: [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR), [OptiScaler](https://github.com/optiscaler/OptiScaler), [clshortfuse/RenoDX](https://github.com/clshortfuse/renodx), [AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (RCAS), [Community Shaders](https://github.com/doodlegabe/CommunityShaders) 팀

이 프로젝트는 MIT 라이선스로 오픈소스화되어 있습니다. 자세한 내용은 [LICENSE](LICENSE)를 참조하세요.
