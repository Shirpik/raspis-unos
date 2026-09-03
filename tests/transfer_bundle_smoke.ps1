param([Parameter(Mandatory=$true)][string]$SolverPath)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("raspis-transfer-" + [guid]::NewGuid().ToString('N'))
$port = Get-Random -Minimum 28081 -Maximum 38080
$baseUrl = "http://127.0.0.1:$port"
$process = $null
try {
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'data') -Force | Out-Null
    foreach ($directory in @('output/latest','output/manual','output/published')) {
        New-Item -ItemType Directory -Path (Join-Path $testRoot $directory) -Force | Out-Null
    }
    $workDays = 1..7 | ForEach-Object { @{day=$_;enabled=($_ -le 6);start_slot=1;end_slot=7} }
    $data = @{
        schema_version = 4
        settings = @{start_date='2026-09-07';end_date='2026-09-12';solver_config=@{week_time_limit_seconds=5;solver_workers=2;solver_max_memory_mb=512}}
        campuses = @(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types = @(@{id=1;name='Classroom';description='Theory'})
        groups = @(@{id=0;name='TEST-101';parts=1;size=20;home_campus=0;work_days=$workDays})
        teachers = @(
            @{id=0;name='Primary teacher';work_days=$workDays},
            @{id=1;name='Substitute teacher';work_days=$workDays}
        )
        rooms = @(@{id=0;name='101';campus=0;room_type=1;capacity=25;equipment=@();active=$true})
        lessons = @(@{id=0;group=0;subgroup=-1;teacher=0;total_hours=2;total_slots=1;name='Mathematics';subject_id=0;is_lab=$false;is_block=$false;allowed_campuses=@(0);week_parity='all';fixed_room=0;allow_room_substitution=$true;required_room_type=1;required_capacity=20;required_equipment=@()})
        unavailable = @()
        teacher_unavailable = @()
        substitutions = @(@{id=0;lesson_id=0;date='2026-09-07';slot=1;absent_teacher=0;substitute_teacher=1;hours=2;reason='Transfer test';status='active'})
        accounting_adjustments = @(@{id=0;teacher_id=1;hours=1;reason='Test adjustment'})
    }
    $autoSchedule = @{
        generated_at='2026-09-01T00:00:00Z'
        groups=@(@{group_index=0;group_name='TEST-101';days=@(@{date='07.09.2026';date_iso='2026-09-07';weekday='MON';slots=@(@{slot=1;time='08:30-09:55';lessons=@(@{id=0;name='Automatic mathematics';teacher_id=0;teacher_name='Primary teacher';room_id=0;room_name='101'})})})})
    }
    $manualSchedule = $autoSchedule | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $manualSchedule.groups[0].days[0].slots[0].lessons[0].name = 'Manual mathematics'
    [IO.File]::WriteAllText((Join-Path $testRoot 'data/timetable_data.json'), ($data | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding($false)))
    $auth = @{username='fake_grid';password_salt='00112233445566778899aabbccddeeff';password_hash='b866e38c02150905dcadf54bf7eddfd833b09c541192b974ea5ea48bd64680d8';iterations=150000}
    [IO.File]::WriteAllText((Join-Path $testRoot 'data/auth_config.json'), ($auth | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $testRoot 'output/latest/schedule_all.json'), ($autoSchedule | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $testRoot 'output/manual/schedule_all.json'), ($manualSchedule | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $testRoot 'output/published/schedule_all.json'), ($autoSchedule | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $testRoot 'output/latest/room_allocation.json'), (@{assigned=1;unassigned=0;substituted=0} | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))

    $process = Start-Process -FilePath $SolverPath -ArgumentList @("$port") -WorkingDirectory $testRoot -PassThru -WindowStyle Hidden
    $ready = $false
    for ($i=0; $i -lt 40; $i++) {
        try { $null = Invoke-RestMethod "$baseUrl/api/auth/status" -TimeoutSec 1; $ready=$true; break } catch { Start-Sleep -Milliseconds 250 }
    }
    if (!$ready) { throw 'API did not start' }
    $webSession = New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $null = Invoke-RestMethod "$baseUrl/api/auth/login" -Method Post -ContentType 'application/json' -WebSession $webSession -Body (@{username='fake_grid';password='fake-grid-pass'} | ConvertTo-Json)
    $PSDefaultParameterValues['Invoke-RestMethod:WebSession'] = $webSession

    $bundle = Invoke-RestMethod "$baseUrl/api/transfer/export"
    if ($bundle.format -ne 'raspis-transfer-bundle' -or $bundle.schema_version -ne 1) { throw 'Transfer bundle format is invalid' }
    if ($bundle.summary.groups -ne 1 -or $bundle.summary.teachers -ne 2 -or $bundle.summary.substitutions -ne 1) { throw 'Transfer summary is invalid' }
    if (!$bundle.schedules.auto -or !$bundle.schedules.manual -or !$bundle.schedules.published) { throw 'Not all schedule variants were exported' }
    if ($bundle.reports.room_allocation.assigned -ne 1) { throw 'Room allocation report was not exported' }

    $null = Invoke-RestMethod "$baseUrl/api/groups/0" -Method Patch -ContentType 'application/json' -Body (@{name='BROKEN-BEFORE-IMPORT'} | ConvertTo-Json)
    $importBody = @{bundle=$bundle;primary_schedule='manual';publish=$true} | ConvertTo-Json -Depth 40
    $result = Invoke-RestMethod "$baseUrl/api/transfer/import" -Method Post -ContentType 'application/json' -Body $importBody
    if (!$result.success -or $result.primary_schedule -ne 'manual' -or !$result.published) { throw 'Transfer import response is invalid' }

    $restoredData = Invoke-RestMethod "$baseUrl/api/data"
    if ($restoredData.groups[0].name -ne 'TEST-101' -or $restoredData.substitutions.Count -ne 1 -or $restoredData.accounting_adjustments.Count -ne 1) { throw 'Full data state was not restored' }
    $activeSchedule = Invoke-RestMethod "$baseUrl/api/schedule"
    $publishedSchedule = Invoke-RestMethod "$baseUrl/api/schedule/published"
    if ($activeSchedule.groups[0].days[0].slots[0].lessons[0].name -ne 'Manual mathematics') { throw 'Manual schedule was not selected as active' }
    if ($publishedSchedule.groups[0].days[0].slots[0].lessons[0].name -ne 'Manual mathematics') { throw 'Imported schedule was not published' }
    $hours = Invoke-RestMethod "$baseUrl/api/hours"
    if (!$hours.schedule_found -or $hours.lessons[0].scheduled_hours -ne 2) { throw 'Hours were not recalculated from imported state' }
    $backups = @(Get-ChildItem -File (Join-Path $testRoot 'output/transfer_backups') -Filter '*.raspis.json')
    if ($backups.Count -ne 1) { throw 'A full pre-import backup was not created' }

    Write-Host 'Transfer bundle: full data, schedules, reports, substitutions, accounting and backup passed.'
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    if ($resolvedTest.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTest).StartsWith('raspis-transfer-', [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTest)) {
        Remove-Item -LiteralPath $resolvedTest -Recurse -Force
    }
}
