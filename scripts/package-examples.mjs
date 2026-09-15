import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { lstat, mkdir, mkdtemp, readFile, readdir, realpath, rename, rm, utimes, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { deterministicTar, deterministicGzip } from './deterministic-tar.mjs';

const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const destination = path.join(repository, 'public/downloads');
const epoch = new Date('2026-09-13T00:00:00Z');
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const sourceExtensions = /\.(?:cpp|h|cs)$/;

// Only original, explicitly named tutorial roots may contribute source files.
// In particular, never walk examples/MMORPG/Plugins as a whole: local lab preparation
// can copy Epic plugins there, and those are not part of the downloadable additions.
const sourceRoots = [
  'examples/MMORPG/Source/MMORPG',
  'examples/MMORPG/Source/MMOContracts',
  'examples/MMORPG/Source/MMOPersistence',
  'examples/MMORPG/Source/MMOBackendService',
  'examples/MMORPG/Source/ThirdParty/MMOThirdParty',
  'examples/MMORPG/Plugins/MMOIntegration/Source/MMOBackendClient',
  'examples/MMORPG/Plugins/MMOIntegration/Source/MMOBackendServer',
  'examples/MMORPG/Plugins/GameFeatures/MMOCore/Source/MMOCore',
  'examples/MMORPG/Plugins/MMODocTools/Source/MMODocTools',
  'examples/MMORPG/Plugins/MMOFramework/Source/MMOFramework',
];
const originalAssets = [
  'examples/MMORPG/Content/Tutorial/BP_MMOCharacter.uasset',
  'examples/MMORPG/Content/Tutorial/BP_MMOTrainingTarget.uasset',
  'examples/MMORPG/Content/Tutorial/GA_ArcaneBolt.uasset',
  'examples/MMORPG/Content/Tutorial/BP_BackendHealth.uasset',
  'examples/MMORPG/Content/UI/BP_MMOUIPolicy.uasset',
  'examples/MMORPG/Content/UI/WBP_Inventory.uasset',
  'examples/MMORPG/Content/UI/WBP_Login.uasset',
  'examples/MMORPG/Content/UI/WBP_PlayerHUD.uasset',
  ...['WBP_MMOLayout', 'WBP_ManaStatus', 'WBP_Settings', 'WBP_Confirm'].map(name => `examples/MMORPG/Content/UI/${name}.uasset`),
  'examples/MMORPG/Content/Localization/ST_MMO.uasset',
  'examples/MMORPG/Content/MMO/BP_MMOGameMode.uasset',
  'examples/MMORPG/Content/MMO/Experiences/DA_MMOExperience.uasset',
  'examples/MMORPG/Content/MMO/Pawns/DA_MMOPlayer.uasset',
  'examples/MMORPG/Content/MMO/Abilities/DA_MMOAbilities.uasset',
  'examples/MMORPG/Content/MMO/Labels/DA_TutorialExamples.uasset',
  'examples/MMORPG/Content/MMO/Input/DA_MMOInputConfig.uasset',
  'examples/MMORPG/Content/MMO/Input/IMC_MMOPlayer.uasset',
  ...['Move', 'Look', 'Target', 'Attack', 'Spell', 'Inventory', 'Settings'].map(name => `examples/MMORPG/Content/MMO/Input/IA_${name}.uasset`),
  ...['ALI_MMOCharacter', 'ABP_MMOUnarmed', 'ABP_MMOCharacter'].map(name => `examples/MMORPG/Content/Characters/MMO/${name}.uasset`),
  'examples/MMORPG/Plugins/GameFeatures/MMOCore/Content/MMOCore.uasset',
];
const officialBlueprintTexts = [
  'public/blueprints/lyra/jump-event-graph.txt',
  'public/blueprints/lyra/dash-direction.txt',
  'public/blueprints/lyra/root-layout-registration.txt',
];
const backendScripts = [
  'Common.ps1', 'Prepare-Dependencies.ps1', 'Initialize-Database.ps1',
  'Build-Backend.ps1', 'Start-Backend.ps1', 'Stop-Backend.ps1',
  'Test-Backend.ps1', 'Test-Backend-Concurrency.ps1', 'Test-Backend-Faults.ps1', 'Test-ClientBoundaries.ps1',
  'Test-InternationalizedData.ps1', 'Test-BackupRestore.ps1',
].map(name => `scripts/backend/${name}`);
const mmoExplicit = [
  'scripts/mmorpg/Advance-Lesson.ps1', 'scripts/mmorpg/lesson_project.py',
  'curriculum/mmorpg/stages.json', 'curriculum/mmorpg/source-book.json',
  ...['00', '01', '02'].map(stage => `curriculum/mmorpg/descriptors/${stage}.uproject`),
  'curriculum/mmorpg/skeleton/Source/MMORPG/MMORPG.cpp',
  'curriculum/mmorpg/skeleton/Source/MMORPG/MMORPG.Build.cs',
  'curriculum/mmorpg/skeleton/Config/DefaultEngine.ini',
  'curriculum/mmorpg/skeleton/Config/DefaultEditorPerProjectUserSettings.ini',
  'NOTICE.md', 'scripts/patches/README.md', 'scripts/patches/blueprint-screenshot-ue58.patch',
  'verification/blueprint-screenshot.json',
  'verification/mmorpg-final-editor-build.json',
  'verification/mmorpg-tagged-editor-build.json',
  'verification/mmorpg-tagged-editor-toolsets-build.json',
  'verification/mmorpg-tagged-editor-startup-attempts.json',
  'verification/mmorpg-tagged-ability-input-assets.json',
  'verification/mmorpg-tagged-ability-input-capture.json',
  'verification/mmorpg-tagged-ability-input-runtime.json',
  'verification/mmorpg-tagged-client-build.json',
  'verification/mmorpg-tagged-server-build.json',
  'verification/mmorpg-stable-zone-editor-toolsets-build.json',
  'verification/mmorpg-stable-zone-client-build.json',
  'verification/mmorpg-stable-zone-server-build.json',
  'verification/mmorpg-stable-zone-issue.json',
  'verification/mmorpg-packaged-smoke-before-stable-zone.json',
  'verification/mmorpg-packaged-smoke.json',
  'verification/mmorpg-packaged-death-logout.json',
  'verification/mmorpg-packaged-rendered-showcase.json',
  'verification/mmorpg-packaged-visual-review.json',
  'verification/mmorpg-packaged-media-sync.json',
  'verification/mmorpg-package-attempt1-zen-proxy.json',
  'verification/mmorpg-package-proxy-recovery.json',
  'verification/mmorpg-package.json',
  'verification/mmorpg-staged-presentation.json',
  'verification/mmorpgclient-final-build.json',
  'verification/mmorpgserver-final-build.json',
  'verification/unrealpak-final-build.json',
  'verification/bootstrappackagedgame-final-build.json',
  'examples/MMORPG/README.md', 'examples/MMORPG/.gitignore',
  'examples/MMORPG/MMORPG.uproject', 'examples/MMORPG/dependencies.json',
  'examples/MMORPG/prepare_content.py', 'examples/MMORPG/ui-designer-contract.json',
  'examples/MMORPG/ui-designer-strings.json',
  'examples/MMORPG/localization-runtime-zh-Hans.json',
  ...['MMORPG', 'MMORPGEditor', 'MMORPGClient', 'MMORPGServer', 'MMOBackendService'].map(name => `examples/MMORPG/Source/${name}.Target.cs`),
  ...['DefaultEngine', 'DefaultEditor', 'DefaultGame', 'DefaultGameplayTags', 'DefaultGameUserSettings', 'DefaultInput'].map(name => `examples/MMORPG/Config/${name}.ini`),
  'examples/MMORPG/Config/Windows/WindowsEngine.ini',
  ...['Gather', 'Export', 'Import', 'Compile', 'GenerateReports'].map(name => `examples/MMORPG/Config/Localization/MMO_${name}.ini`),
  'examples/MMORPG/Plugins/MMOIntegration/MMOIntegration.uplugin',
  'examples/MMORPG/Plugins/GameFeatures/MMOCore/MMOCore.uplugin',
  'examples/MMORPG/Plugins/MMODocTools/MMODocTools.uplugin',
  'examples/MMORPG/Plugins/MMODocTools/README.md',
  'examples/MMORPG/Plugins/MMOFramework/MMOFramework.uplugin',
  'examples/MMORPG/Plugins/MMOFramework/README.md',
  'examples/MMORPG/Plugins/MMOFramework/api-contract.json',
  ...['README.md', 'config.example.ps1', 'openapi.json', 'source-manifest.json', 'validation.json',
    'http-test-results.json', 'fault-test-results.json', 'client-boundary-review.json', 'concurrency-test-results.json'].map(name => `examples/MMORPG/backend/${name}`),
  'scripts/prepare-labs.ps1', 'scripts/set-preview-quality.ps1', 'scripts/mmorpg/Invoke-Smoke.ps1',
  'scripts/mmorpg/Start-Lab.ps1', 'scripts/mmorpg/Stop-Lab.ps1',
  'scripts/mmorpg/Package-Lab.ps1', 'scripts/mmorpg/Build-Localization.ps1',
  'scripts/mmorpg/Invoke-RenderedShowcase.ps1', 'scripts/mmorpg/recompile-gameplay-blueprints.py',
  'scripts/mmorpg/fix-button-label-slots.py',
  'scripts/mmorpg/fix-animation-speed-axis.py', 'scripts/mmorpg/validate-animation.py',
  'scripts/mmorpg/validate-existing-content.py',
  'scripts/mmorpg/prepare-tagged-ability-inputs.py',
  'scripts/mmorpg/Read-TaggedAbilityInputs.ps1',
  'scripts/mmorpg/Read-TaggedAbilityDispatch.ps1',
  'scripts/mmorpg/Test-MovementBaseLogs.ps1',
  'scripts/mmorpg/translate-localization.py', 'scripts/mmorpg/prepare-health-cook-label.py',
  'scripts/mmorpg/validate-health-cook-label.py',
  'scripts/mmorpg/Test-StagedPresentation.ps1', 'scripts/mmorpg/Start-EditorUIValidation.ps1',
  'scripts/mmorpg/inspect-runtime-ui.py', 'scripts/mmorpg/runtime_ui_toolset.py',
  'scripts/mmorpg/summarize-native-ui-validation.py',
  'public/blueprints/mmorpg/backend-health.txt', 'verification/backend-health-roundtrip.json',
  'verification/backend-health-screenshot.json', 'verification/mmorpg-rendered-showcase.json',
  'verification/mmorpg-rendered-media-review.json',
  'verification/mmorpg-assets.json', 'verification/mmorpg-smoke.json', 'source-evidence/mmorpg-references.json',
  'verification/backend-walkthrough-review.json',
  'source-evidence/configuration-mmorpg.json', 'verification/configuration-mmorpg-review.json',
  'verification/localization-script-review.json',
  'verification/ui-designer-assets.json', 'verification/mana-status-bindings.json',
  ...['WBP_MMOLayout', 'WBP_Login', 'WBP_ManaStatus', 'WBP_PlayerHUD', 'WBP_Inventory', 'WBP_Settings', 'WBP_Confirm'].map(name => `verification/${name}-designer.json`),
  'verification/mmorpg-animation-assets.json', 'verification/mmorpg-character-defaults.json',
  'verification/mmorpg-experience-assets.json', 'verification/mmorpg-game-mode.json',
  'verification/mmorpg-original-assets-review.json', 'source-evidence/mmorpg-native-graphs.json',
  'verification/mmorpg-death-blueprints.json', 'verification/mmorpg-death-assets-sync.json', 'verification/mmorpg-death-logout.json',
  'verification/mmorpg-button-slots.json', 'verification/mmorpg-button-slots-sync.json', 'verification/slate-input-tool-source-review.json',
  'verification/mmorpg-animation-speed-axis.json',
  'source-evidence/mmorpg-graph-captures.json', 'verification/mmorpg-graph-captures.json',
  'verification/mmorpg-graph-capture-actions.jsonl',
  'verification/mmorpg-native-ui-input.json', 'verification/mmorpg-native-input-actions.jsonl',
  'verification/mmorpg-layer-transition-review.json',
  'verification/mmorpg-health-cook-label.json', 'verification/mmorpg-health-cook-label-sync.json',
  'verification/mmorpg-health-cook-label-scan.json', 'verification/mmorpg-existing-content-validation.json',
  'verification/mmorpg-localization.json', 'source-evidence/mmorpg-localization.json',
  'verification/backend-internationalized-data.json', 'verification/backend-backup-restore.json',
  'examples/MMORPG/Content/Localization/MMO/MMO.manifest',
  'examples/MMORPG/Content/Localization/MMO/MMO.locmeta',
  'examples/MMORPG/Content/Localization/MMO/MMO.csv',
  'examples/MMORPG/Content/Localization/MMO/MMO_Conflicts.txt',
  ...['en', 'zh-Hans'].flatMap(culture => ['archive', 'po', 'locres'].map(extension => `examples/MMORPG/Content/Localization/MMO/${culture}/MMO.${extension}`)),
  'public/blueprints/mmorpg/mmo-character-animgraph.txt', 'public/blueprints/mmorpg/mmo-unarmed-locomotion.txt',
  ...backendScripts, ...originalAssets,
];
const trainingExplicit = [
  'examples/LyraTraining/prepare_training.py',
  'examples/LyraTraining/Plugins/GameFeatures/TrainingRange/TrainingRange.uplugin',
  'examples/LyraTraining/Plugins/GameFeatures/TrainingRange/README.md',
  'examples/LyraTraining/Plugins/LyraDocTools/LyraDocTools.uplugin',
  'examples/LyraTraining/Plugins/LyraDocTools/README.md',
  'examples/LyraTraining/Config/TutorialWindowsSM6.ini',
  'scripts/package-training.ps1', 'scripts/verify-training-package.ps1',
  'scripts/verify-training-prepare.py',
  'verification/training-editor-final-build.json',
  'verification/training-package-editor-refresh.json',
  'verification/training-package.json',
  'verification/training-package-runtime.json',
  'verification/training-package-runtime-first-run.json',
  'verification/training-package-log-analysis.json',
  'verification/training-packaged-visual-review.json',
  'verification/unrealpak-final-build.json',
  'verification/bootstrappackagedgame-final-build.json',
  'verification/training-navigation-helper-build.json',
  'verification/training-navigation-helper-sync.json',
  'verification/training-prepare-rerun.json',
  'verification/training-final-capture-settings.json',
  'verification/training-final-preview-state.json',
  'verification/training-final-editor-closure.json',
  'scripts/prepare-labs.ps1', 'scripts/set-preview-quality.ps1', 'scripts/verify-training-blueprints.py',
  'scripts/verify-training-play.py', 'verification/training-blueprint-roundtrip.json', 'verification/training-play.json',
  'scripts/verify-training-network.py', 'verification/training-network.json',
  'scripts/verify-training-lifecycle.py', 'verification/training-lifecycle.json',
  'verification/training-navigation.json', 'verification/training-assets.json',
  'verification/training-cook.json', 'verification/training-configuration-files.json',
  'source-evidence/configuration-training.json',
  'source-evidence/training-packaged-runtime-implementation.json',
  'source-evidence/training-references.json',
  'source-evidence/root-layout-blueprint.json', 'verification/root-layout-graph-export.json',
  'NOTICE.md', 'scripts/patches/README.md', 'scripts/patches/blueprint-screenshot-ue58.patch',
  'verification/blueprint-screenshot.json', ...officialBlueprintTexts,
];

const forbiddenSegments = new Set(['.deps', '.local', 'intermediate', 'binaries', 'saved', 'deriveddatacache', 'node_modules', '__pycache__']);
async function inspect(relative) {
  const pieces = relative.split('/');
  if (pieces.some(part => forbiddenSegments.has(part.toLowerCase()) || /^\.env(?:\.|$)/i.test(part)) || pieces.includes('..')) throw new Error(`Disallowed package path: ${relative}`);
  const absolute = path.resolve(repository, relative);
  if (!absolute.startsWith(repository + path.sep)) throw new Error(`Path escaped repository: ${relative}`);
  const info = await lstat(absolute);
  if (info.isSymbolicLink() || await realpath(absolute) !== absolute) throw new Error(`Package inputs must not use symlinks: ${relative}`);
  return { absolute, info };
}
async function collect(relative, allowFile) {
  const { absolute } = await inspect(relative);
  const output = [];
  for (const entry of await readdir(absolute, { withFileTypes: true })) {
    const child = `${relative}/${entry.name}`;
    if (entry.isSymbolicLink()) throw new Error(`Symlink under allowed source root: ${child}`);
    if (entry.isDirectory() && !forbiddenSegments.has(entry.name.toLowerCase())) output.push(...await collect(child, allowFile));
    else if (entry.isFile() && allowFile(entry.name)) output.push(child);
  }
  return output;
}
function inspectText(relative, bytes) {
  if (relative.endsWith('.uasset')) return;
  const text = bytes.toString('utf8');
  if (/-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----/.test(text)
    || /postgres(?:ql)?:\/\/[^\s:/]+:[^\s@]+@/i.test(text)
    || /(?:PGPASSWORD|MMO_SERVER_SECRET)\s*=\s*['"](?!<|\$)[^'"\r\n]+['"]/i.test(text)) {
    throw new Error(`Possible populated credential in allowed source: ${relative}`);
  }
}
const packageReadme = (kind) => `# ${kind === 'mmorpg' ? 'MMORPG 原创教学工程' : 'Lyra 射击训练场教学增量'}

本包来自 Lyra 学习手册：https://wangshuxian6.github.io/Lyra/ 。主体为原创教学增量，不包含官方 Lyra 的 .uasset/.umap、完整源码插件、美术、地图或第三方库二进制。训练包另含明确标注来源的官方蓝图原生节点文本。

保留当前 examples/ 与 scripts/ 目录层次。在 Windows PowerShell 7 中，从本文件所在目录运行：

    ./scripts/prepare-labs.ps1 -LyraRoot 'F:/UE/LyraStarterGame' -LabsRoot 'F:/UE/LyraDocLabs' -Lab ${kind === 'mmorpg' ? 'MMORPG' : 'Training'}

先从 Epic 获取与你的引擎对应的 Lyra。脚本只从指定的本地官方项目复制依赖，并在 LabsRoot 下准备实验工程。训练场的地图、Experience 和 PawnData 由 prepare_training.py 在本地实验副本中生成，不分发官方资产的副本。
${kind === 'mmorpg' ? `\nMMORPG 包含 ${originalAssets.length} 个原创教学 .uasset、C++/Config、Game Feature、独立后端 Program、SQL 迁移与验证脚本。详细构建/运行步骤见 examples/MMORPG/README.md 和 examples/MMORPG/backend/README.md。第三方依赖由 scripts/backend/Prepare-Dependencies.ps1 下载/定位，数据库凭据在本机初始化时生成，包中没有预置密码。\n` : '\npublic/blueprints/lyra/ 下的 Jump EventGraph、Dash SelectDirectionalMontage 和 W_OverallUILayout EventGraph 文本来自本机 Epic Lyra 原始蓝图，供 scripts/verify-training-blueprints.py 在完整依赖的实验副本中导入编译。它们不是本项目原创蓝图；具体原始资产、图名、文件哈希见 MANIFEST.json 的 externalMaterials，并参阅 NOTICE.md。请保留 public/ 目录层次。\n'}
MANIFEST.json 记录包内每个源文件的相对路径、字节数和 SHA-256。下载页另提供整个 tar.gz 的 SHA256SUMS.txt。源代码文件和资产并不等同于所有运行场景已经通过；查看随包验证记录及教程当前验收说明。
`;

await mkdir(destination, { recursive: true });
const temporaryPrefix = path.join(os.tmpdir(), 'lyra-example-packages-');
const scratch = await mkdtemp(temporaryPrefix);
try {
  const originalAssetEvidence = JSON.parse(await readFile(path.join(repository, 'verification/mmorpg-assets.json'), 'utf8'));
  const recordedAssets = new Map(originalAssetEvidence.assets.map(entry => [entry.repoPath, entry]));
  if (recordedAssets.size !== originalAssets.length || originalAssets.some(relative => !recordedAssets.has(relative))) {
    throw new Error('Original asset evidence and the explicit package asset allowlist differ. Review new assets before packaging.');
  }
  for (const relative of originalAssets) {
    const { absolute } = await inspect(relative);
    const bytes = await readFile(absolute);
    const recorded = recordedAssets.get(relative);
    if (bytes.length !== recorded.bytes || digest(bytes) !== recorded.sha256.toLowerCase()) {
      throw new Error(`Original asset changed after native validation: ${relative}`);
    }
  }
  const sourceEvidence = JSON.parse(await readFile(path.join(repository,'source-evidence/lyra-references.json'),'utf8'));
  const externalMaterials = [];
  for (const relative of officialBlueprintTexts) {
    const entry = sourceEvidence.media.find(item => `public${item.publicPath}` === relative && item.kind === 'native-node-text');
    const { absolute } = await inspect(relative);
    if (!entry || digest(await readFile(absolute)) !== entry.sha256) throw new Error(`Official Blueprint text needs current source evidence: ${relative}`);
    externalMaterials.push({ path:relative, origin:'Epic Lyra local baseline; not original tutorial content', sourceAsset:entry.sourceAsset, graph:entry.graph, sha256:entry.sha256, notice:'NOTICE.md' });
  }
  const mmoSources = (await Promise.all(sourceRoots.map(root => collect(root, name => sourceExtensions.test(name))))).flat();
  const trainingSources = (await Promise.all([
    'examples/LyraTraining/Plugins/LyraDocTools/Source',
    'examples/LyraTraining/Plugins/GameFeatures/TrainingRange/Source/TrainingRangeVerification',
  ].map(root => collect(root, name => sourceExtensions.test(name))))).flat();
  const migrations = await collect('examples/MMORPG/backend/migrations', name => /^\d{3}_[a-z0-9_]+\.sql$/.test(name));
  const definitions = [
    { id:'lyra-mmorpg-examples', kind:'mmorpg', title:'MMORPG 原创教学工程与 UE 后端', files:[...mmoExplicit,...mmoSources,...migrations] },
    { id:'lyra-training-examples', kind:'training', title:'Lyra 射击训练场原创教学增量', files:[...trainingExplicit,...trainingSources] },
  ];
  const packages = [];
  for (const definition of definitions) {
    const root = path.join(scratch, definition.id);
    const entries = [];
    for (const relative of [...new Set(definition.files)].sort()) {
      const { absolute, info } = await inspect(relative);
      if (!info.isFile()) throw new Error(`Expected a regular package file: ${relative}`);
      const bytes = await readFile(absolute); // One read supplies both the staged bytes and their digest.
      inspectText(relative, bytes);
      const staged = path.join(root, relative);
      await mkdir(path.dirname(staged), { recursive:true });
      await writeFile(staged, bytes, { mode:0o644 }); await utimes(staged, epoch, epoch);
      entries.push({ path:relative, bytes:bytes.length, sha256:digest(bytes) });
    }
    const readme = Buffer.from(packageReadme(definition.kind));
    await writeFile(path.join(root, 'README.md'), readme, { mode:0o644 }); await utimes(path.join(root,'README.md'), epoch, epoch);
    entries.push({ path:'README.md', bytes:readme.length, sha256:digest(readme) });
    entries.sort((a,b) => a.path.localeCompare(b.path, 'en'));
    const manifest = { schemaVersion:1, id:definition.id, title:definition.title, engine:'UE 5.8.1',
      originalAssetCount:definition.kind === 'mmorpg' ? originalAssets.length : 0,
      externalMaterials:definition.kind === 'training' ? externalMaterials : [],
      policy:'Explicit original-source roots and script/config/asset paths; separately attributed official Blueprint node text; no official Lyra binary assets, third-party binaries, build output or populated credentials.', files:entries };
    const manifestBytes = Buffer.from(JSON.stringify(manifest,null,2)+'\n');
    await writeFile(path.join(root,'MANIFEST.json'), manifestBytes, { mode:0o644 }); await utimes(path.join(root,'MANIFEST.json'), epoch, epoch);
    const archiveEntries = [...entries.map(entry => `${definition.id}/${entry.path}`), `${definition.id}/MANIFEST.json`].sort();
    const filename = `${definition.id}.tar.gz`;
    const archive = path.join(scratch,filename);
    // Node omits the current timestamp from the gzip header, avoiding new download
    // hashes solely because unchanged source files were packaged again later.
    await writeFile(archive,deterministicGzip(await deterministicTar(scratch,archiveEntries,epoch)));
    const actualEntries = execFileSync('tar',['-tzf',archive],{encoding:'utf8'}).trim().split(/\r?\n/).sort();
    if (JSON.stringify(actualEntries) !== JSON.stringify(archiveEntries)) throw new Error(`Archive inventory mismatch: ${filename}`);
    const bytes = await readFile(archive);
    const manifestFilename = `${definition.id}.manifest.json`;
    await writeFile(path.join(destination,manifestFilename),manifestBytes);
    const archiveDestination = path.join(destination,filename);
    const existingArchive = await readFile(archiveDestination).catch(error => { if (error.code === 'ENOENT') return null; throw error; });
    // Regenerate and validate every archive, but leave identical public bytes in
    // place: Windows can deny replacing a file while another reader has it open.
    if (!existingArchive?.equals(bytes)) {
      // Keep unfinished files outside public so a later export cannot copy them.
      // The repository-local staging directory also avoids the system temp drive.
      const archiveStaging = path.join(repository,'.local','package-staging');
      await mkdir(archiveStaging,{recursive:true});
      const stagedArchive = path.join(archiveStaging,`${filename}.${process.pid}.tmp`);
      try {
        await writeFile(stagedArchive,bytes);
        await rename(stagedArchive,archiveDestination);
      } finally {
        await rm(stagedArchive,{force:true});
      }
    }
    packages.push({ id:definition.id, title:definition.title, filename, bytes:bytes.length, sha256:digest(bytes),
      fileCount:archiveEntries.length, originalAssetCount:manifest.originalAssetCount, manifest:manifestFilename });
    console.log(`Packaged ${filename}: ${archiveEntries.length} allowlisted files, ${bytes.length} bytes, ${manifest.originalAssetCount} original assets.`);
  }
  await writeFile(path.join(destination,'SHA256SUMS.txt'),packages.map(pkg => `${pkg.sha256}  ${pkg.filename}`).join('\n')+'\n');
  await writeFile(path.join(destination,'packages.json'),JSON.stringify({schemaVersion:1,packages},null,2)+'\n');
  // Keep the human-facing inventory truthful when later source additions change
  // the allowlist. Only this explicitly generated table is rewritten.
  const downloadPage = path.join(repository, 'content/docs/downloads.mdx');
  const contents = await readFile(downloadPage, 'utf8');
  const tableMarker = /\{\/\* package-table:start \*\/\}[\s\S]*?\{\/\* package-table:end \*\/\}/;
  if (!tableMarker.test(contents)) throw new Error('Download page is missing its generated package table markers.');
  const descriptions = [
    `C++、Config、${originalAssets.length} 个原创教学资产、Designer/MVVM、动画与输入增量、MMOFramework/MMODocTools 插件、后端 Program、SQL 迁移和验证脚本`,
    'TrainingRange 及 Development 打包运行验收模块、LyraDocTools Editor 插件、原生资产准备、蓝图导入/玩法/联机验证与记录；另附注明 Epic 来源的三份原生节点文本',
  ];
  const rows = packages.map((pkg,index) => `| [${pkg.title}（tar.gz）](/downloads/${pkg.filename}) | ${descriptions[index]} | ${pkg.fileCount} | [逐文件 SHA-256 清单](/downloads/${pkg.manifest}) |`);
  const table = ['{/* package-table:start */}', '| 下载包 | 包含内容 | 文件数 | 文件清单 |', '| --- | --- | --- | --- |', ...rows, '{/* package-table:end */}'].join('\n');
  const updated = contents.replace(tableMarker,table);
  if (updated !== contents) await writeFile(downloadPage,updated);
} finally {
  // Never recursively remove a computed path before checking its resolved containment.
  const resolved = path.resolve(scratch);
  if (!resolved.startsWith(path.resolve(temporaryPrefix)) || path.dirname(resolved) !== path.resolve(os.tmpdir())) throw new Error('Unsafe temporary cleanup target');
  await rm(resolved,{recursive:true,force:true});
}
