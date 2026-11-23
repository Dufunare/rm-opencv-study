# RoboMaster 装甲板识别与位姿解算考核说明

程序用 C++17 和 OpenCV 4.14.0 分析图片与录制视频。画面标注装甲板颜色、贴纸编号、四个灯条端点、位置和姿态轴。CSV 保留每个点的像素坐标、毫米位置、旋转向量和重投影误差。视频可回看匀速预测与之后观测的差异。

现有素材缺少可以确认与录制画面一一对应的相机标定。当前演示会在画面上标出 `approximate camera`。报告中的位置和姿态是基于假设内参和灯条尺寸的估计值。现场答辩可以接着运行已知内参和位姿的合成自测，说明如何验证 PnP 数学链路。

## 1. 复现和提交材料

从仓库根目录运行。构建沿用 `D:\Dev\environment` 中的原生 Windows C++ 工具链约定，包括 VS 2026、MSVC 14.51、Windows SDK 10.0.26100.0 和 VS 随附的 CMake、vcpkg。本机已有 `CMakeUserPresets.json` 和 vcpkg 构建产物时可直接构建 Release。首次配置可运行 `.\scripts\Setup-Project.ps1 -Build`，它还会运行原有的合成灯条自测。

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build --preset release --target armor_study
$study = '.\out\build\windows-vs2026-x64\Release\armor_study.exe'
& $study --self-test
```

`out/models/stickers.yml.gz` 是约 3.1 MB 的 HOG 模板库。本机已生成该文件。换机时可随演示材料提供，或用以下步骤重建。Python 环境由现有 `datas` 项目和 `uv.lock` 管理。原始数据在 Hugging Face 缓存中时使用下列命令。没有缓存时，可在首次提取时加 `--download`，或对两个脚本传入 `--archive <tar路径>`。视频直接取自表中的上游文件。

```powershell
uv sync --project datas --locked
uv run --project datas --locked python datas/extract_samples.py
uv run --project datas --locked python datas/prepare_stickers.py --holdout datas/samples/mixed_armor --out out/sticker-training
& $study --train out/sticker-training/train.tsv
```

| 材料 | 路径或获取方式 |
| --- | --- |
| 图片与核验用标签 | `datas/samples/mixed_armor/`，`rmcv/armor` 的 `mixed_armor` 分片，固定版本 `b1ea191eb475102b31e6890db46a210c8c2982d0`。[数据集与标注说明](https://huggingface.co/datasets/rmcv/armor) |
| 远距离平移演示 | `datas/videos/远距离平移.avi`，来自深圳大学 RobotPilots。[上游测试视频目录](https://github.com/SZURPVision/26_NNDeployment_Lib_and_Detection_Models#42-测试视频) |
| 第二段视频 | `datas/videos/vid_move_003.mp4`，来自 `rzhu98-hub/RM_AutoAim`。[上游视频](https://github.com/rzhu98-hub/RM_AutoAim/blob/main/armor_detection_py/video/vid_move_003.mp4) |
| 运行配置 | `config/plates.yml`、`out/models/stickers.yml.gz`。如有录制相机的标定结果，再提供对应的相机 YAML。格式见 `config/camera-example.yml`。 |
| 现场备份 | `demo_output/assessment/` 下的 `annotated.png`、`annotated.avi`、`detections.csv`、`forecast.csv` 和 `run.txt`。 |

`mixed_armor` 汇集多个原始出处未逐一确认的来源。数据卡中的 CC BY 4.0 覆盖整理后的标注，原图版权情况见数据卡。提交时按招新通知提供来源链接或获准随附的材料。 [数据集授权说明](https://huggingface.co/datasets/rmcv/armor#7-授权)

## 2. 现场演示顺序

**先讲一张图。** 放大蓝色 3 号板，指出画面的 `0、1、2、3` 分别对应左上、左下、右下、右上四个灯条中心线端点。`t[m]` 是板中心在相机坐标系的位置。`r[rad]` 是轴角旋转向量。可打开 `detections.csv` 对照像素和位姿数值。

```powershell
& $study --image datas/samples/mixed_armor/mixed_armor_000261.jpg --out demo_output/assessment/image
```

**再播远距离平移。** 空格暂停，`Q` 或 `Esc` 退出。黄色十字显示之前预测在当前帧应出现的位置。黄线连接它与本帧检测中心。首段约 3 秒，提前 3 帧相当于 20 ms。现场要保存标注视频时加 `--write-video`，离线批处理时加 `--no-gui`。

```powershell
& $study --video 'datas/videos/远距离平移.avi' --forecast-frames 3 --max-frames 450 --out demo_output/assessment/far
& $study --video datas/videos/vid_move_003.mp4 --forecast-frames 3 --max-frames 450 --out demo_output/assessment/move
```

**最后打开 CSV 和代码。** `run.txt` 给出画面尺寸、视频帧率、K、畸变系数和统计。`detections.csv` 每行记录一个检出，帧号从 0 开始。`forecast.csv` 每行记录一次到期对照，带起始帧、对照帧、预测间隔、速度、预测和观测位置、两种误差。可运行 `--self-test` 展示已知几何和内参下的回算。

## 3. 代码数据流

| 模块 | 讲解时打开的函数 | 主要工作 |
| --- | --- | --- |
| 读帧与输出 | `src/study.cpp` 的 `runMedia` | 图片解码或视频逐帧读取，加载内参与模板，画标注，导出 CSV 和可选 MJPEG 视频。Windows 解码后端遇到系统代码页无法表示的路径时，程序会在 `out/` 下为输入临时建立可解码路径。 |
| 颜色与灯条 | `src/detector.cpp` 的 `detectPlates`、`collectLights` | 利用 B−R 和 R−B 的通道差与亮度筛出红蓝光。高亮趋白时，以亮芯定位，参考邻近颜色判色。轮廓经最小外接矩形得到中心线与两端。 |
| 装甲板配对 | `detectPlates` | 限制同色、长度比、平行度、横向间距和沿灯条错位。按几何与贴纸相似度排序，同一根灯条只交给一块板。 |
| 贴纸识别 | `stickerFeature`、`StickerModel::classify` | 将四个点透视变换到统一坐标，取两灯之间且上下略扩展的区域。用 HOG 表示笔画的梯度方向，按最近模板距离加权投票。支持 1、2、3、4、哨兵、前哨站和基地。模板数据中缺少可用的 5 号样本。投票不明确时显示 `unknown`。 |
| 位姿 | `src/pose.cpp` 的 `chooseSize`、`solvePose` | 按四点长度比估计大小板，或通过 `--plate-size small/large` 指定。用 IPPE 求平面 PnP 候选，筛掉负深度，并比较像素重投影误差。 |
| 匀速预测 | `src/forecast.hpp` 的 `MotionStudy` | 按颜色、大小和近邻中心关联连续帧。对 5 至 8 个位置做时间线性拟合，用 `p(t)+vΔt` 推到指定帧，之后和该帧的 PnP 观测比较。断轨会清除历史和待对照的预测。 |

主要初始阈值为红蓝通道差 35、主通道亮度 115、亮芯 G 通道 175、匹配灯条长度比 0.58。取这些阈值是为了在不同曝光下同时保留颜色边缘与亮芯，并抑制长条反光。修改时逐项记录亮灯匹配数、额外检出数和端点偏差，避免只凭一段视频调整。

## 4. 相机、尺寸和位姿误差

物体原点在四个灯条端点构成的矩形中心。正对贴纸看，装甲板的 `+X` 向右、`+Y` 向下，`+Z` 按右手系指向板内。输入 PnP 的对象点是 `(-w/2,-h/2,0)`、`(-w/2,+h/2,0)`、`(+w/2,+h/2,0)`、`(+w/2,-h/2,0)`，依次配对到 LT、LB、RB、RT。相机坐标系 `+X` 向右、`+Y` 向下、`+Z` 向前。`X_cam = R·X_plate + t`，`R` 由 Rodrigues 旋转向量得到。向量方向为旋转轴，模长为角度，单位 rad。 [OpenCV PnP 约定](https://docs.opencv.org/4.x/d5/d1f/calib3d_solvePnP.html)

`config/plates.yml` 以毫米配置灯条中心线间距和有效发光长度，小板按 132 × 55 mm，大板按 225 × 55 mm。它们是演示采用的工作尺寸。DJI 制作规范给出的 135 mm、230 mm 是外形宽度。拿到实物后应测量与当前四点定义对应的两根灯条中心距和发光长度。斜视会缩短二维宽度，大板也可能被 `auto` 当作小板。 [DJI 2026 制作规范手册，第 3.6 节](https://bbs-web-static.robomaster.com/715e2519e5384666a5d13edda72ad43b1767087376172/RoboMaster%202026%20%E6%9C%BA%E7%94%B2%E5%A4%A7%E5%B8%88%E9%AB%98%E6%A0%A1%E7%B3%BB%E5%88%97%E8%B5%9B%E6%9C%BA%E5%99%A8%E4%BA%BA%E5%88%B6%E4%BD%9C%E8%A7%84%E8%8C%83%E6%89%8B%E5%86%8CV1.3.0%EF%BC%8820260209%EF%BC%89.pdf)

默认 `fx=fy=0.9×max(width,height)`，主点放在画面中心，畸变设为零。`--camera` 可加载对应镜头和画面的标定结果。等比例缩放时程序换算焦距与主点，画面发生裁剪或更改 ROI 时需根据实际裁剪位置另行换算。用同一相机、焦距和录制分辨率拍摄多角度棋盘格，可生成 YAML。下面的 `9x6` 指内角点列数和行数，`25` 代表实测每格 25 mm。

```powershell
& $study --calibrate datas/calibration --board 9x6 --square-mm 25 --out out/camera.yml
& $study --video '<与标定对应的视频>' --camera out/camera.yml --plate-size small
```

重投影 RMS 衡量四个模型点投回图像后离输入点多远。它不能单独验证实际距离。比如近似正视时有 `Z≈fx·w/像素宽度`，焦距和物理宽度偏大都会推高距离，过曝让灯条轮廓增长会影响距离和姿态。平面目标可能有两个相近解，CSV 的 `alt_rms_px` 保留可用的备选解误差。两解误差接近时，应结合视角与连续帧判断姿态可信度。

## 5. 实测和答辩问题

图片验证使用原有 20 张开发图。HOG 模板训练排除了这 20 张所在的 9 个采集序列。配对门限曾用这些图排查误检，因此这一组数字属于开发验证。运行时只读图像，评估脚本才读取 JSON。按平均四角点偏差不超过标注灯条平均长度的 35% 做一对一匹配，得到 25 个红蓝亮灯参考中的 21 个匹配，28 个输出中有 21 个匹配。匹配目标的颜色与贴纸均为 21/21，四点平均误差 2.34 px，匹配位姿的重投影 RMS 中位数 0.51 px。图中还有 3 个灭灯参考，当前亮灯流程不覆盖这类目标。

```powershell
uv run --project datas --locked python datas/evaluate_images.py --exe $study --samples datas/samples/mixed_armor --out demo_output/assessment/eval
```

| 视频前 450 帧 | 有检出的帧 | 预测间隔 | 有效对照 | 匀速预测平均差 | 静止基线平均差 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `远距离平移.avi`，150 fps | 364 | 20 ms | 295 | 24.48 mm | 43.49 mm |
| `vid_move_003.mp4`，100 fps | 270 | 30 ms | 93 | 60.18 mm | 51.96 mm |

两列误差都对照之后的 PnP 观测。静止基线把预测起始帧的位置保持到对照帧。两种方法只在相同的连续跟踪样本上比较。第二段视频的结果表明，相机相对运动、深度抖动、关联错误和短时加减速都会影响匀速假设。合成自测的回算误差约为 0.00015 mm 和 0.00000049 rad，验证的是点序、坐标变换和数值求解。

| 考官可能问 | 可验证的回答 |
| --- | --- |
| 怎样知道读出来的数字来自像素？ | `runMedia` 只传 BGR 图和检测四点给 HOG 程序。`evaluate_images.py` 在程序退出后读取标注做比较。 |
| 为什么两个颜色阈值之外还要亮芯？ | 饱和时三个通道都可能很亮，通道差会缩小。看近景蓝色 3 号图，亮芯估计位置，周围颜色确认红蓝。 |
| 为什么使用 IPPE？ | 四个物体点共面。IPPE 适用于平面目标，能返回两个位姿候选。可用正深度和重投影误差核对。 |
| 怎样说明预测有效？ | 先指出 `origin_frame`、`comparison_frame` 和 `horizon_s`，再对照同一批样本的静止基线。展示两个视频的不同结果。 |
| 最先改哪处？ | 先拿到与视频匹配的相机内参和实测灯条尺寸，再处理斜视、反光及漏检。恒速旋转可以在单独的受控视频中实验，需要假设固定旋转中心、半径和角速度，并验证遮挡前后的同板身份。 |

仓库原有的 `armor_demo`、构建预设和提取样例为起点。本次新增灯条配对、HOG 文本模板、PnP、相机标定入口、视频输出、预测回看和评估脚本，使用了 AI 编程协作。外部项目用于获取素材和查阅格式。答辩时现场运行程序，并按照招新要求说明实际使用的协助、自己复测的内容和能解释的代码。

## 6. YOLO Pose 探索

已验证四关键点数据导出，得到 192 张训练图、254 个训练目标，以及 20 张验证图、28 个验证目标。训练覆盖 85 个采集序列，和验证序列无交集。导出为单类 `plate`、`kpt_shape: [4, 3]`，关键点依次为 LT、LB、RB、RT。分类仍可复用颜色和 HOG 模块，先对比神经网络与灯条检测的角点质量。为保持数字和四点语义，训练时关闭水平和垂直翻转。 [Ultralytics Pose 数据格式](https://docs.ultralytics.com/datasets/pose/)

```powershell
uv run --project datas --locked python datas/export_yolo_pose.py --holdout datas/samples/mixed_armor --out out/yolo-pose
```

导出目录已有时，另指定一个新目录即可复跑。以下是后续训练的试跑命令。本轮验证到数据准备，未训练 YOLO 权重、未测准确率。当前沙箱访问 PyPI 受到限制，训练环境和预训练权重需在可联网环境中准备。

```powershell
uv run --project datas --with ultralytics yolo pose train model=yolo26n-pose.pt data=out/yolo-pose/dataset.yaml epochs=5 imgsz=640 batch=4 device=cpu workers=0 fliplr=0.0 flipud=0.0
```

接入时先把网络输出恢复为原图坐标，核对四点顺序和像素单位，再用相同的 `solvePose` 对照两种方法。固定验证帧，比较目标匹配数、亮灯和灭灯召回、四点误差以及推理耗时。可进一步试验外部四点权重，RobotPilots 的 `Infantry-v8n-20260726` 提供 ONNX FP16 和自定义的颜色、类别、四点输出。在本机部署前需核查模型输出契约和运行时兼容性。 [RobotPilots 模型说明](https://github.com/SZURPVision/26_NNDeployment_Lib_and_Detection_Models#41-装甲板四关键点模型) · [Ultralytics ONNX 导出](https://docs.ultralytics.com/modes/export/)
