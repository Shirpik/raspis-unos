param([Parameter(Mandatory=$true)][string]$SolverPath)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("raspis-daily-limits-" + [guid]::NewGuid().ToString('N'))
$port = Get-Random -Minimum 28081 -Maximum 38080
$baseUrl = "http://127.0.0.1:$port"
$process = $null

function New-WorkDays {
    1..7 | ForEach-Object {
        @{day=$_; enabled=($_ -le 6); start_slot=1; end_slot=7}
    }
}

function New-OneSlotWorkDays {
    1..7 | ForEach-Object {
        @{day=$_; enabled=($_ -eq 4); start_slot=1; end_slot=1}
    }
}

function New-SingleWeekdayWorkDays([int]$weekday) {
    1..7 | ForEach-Object {
        @{day=$_; enabled=($_ -eq $weekday); start_slot=1; end_slot=7}
    }
}

function New-Lesson([int]$id, [int]$group, [int]$subgroup, [int]$teacher) {
    @{
        id=$id; uid="lesson-$id"; group=$group; subgroup=$subgroup; teacher=$teacher
        total_hours=2; total_slots=1; name="Subject $id"; subject_id=$id
        is_lab=$false; is_block=$false; is_pp=$false
        allowed_campuses=@(0); week_parity='all'; fixed_room=-1
        allow_room_substitution=$true; required_room_type=0
        required_capacity=0; required_equipment=@()
    }
}

function Get-LatestCandidateReport([string]$name) {
    $file = Get-ChildItem (Join-Path $testRoot 'output/candidates') -Filter $name -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (!$file) { throw "Failed candidate did not preserve $name" }
    Get-Content $file.FullName -Raw | ConvertFrom-Json
}

function Wait-Generation {
    for ($i=0; $i -lt 120; $i++) {
        $progress = Invoke-RestMethod "$baseUrl/api/schedule/progress"
        if ($progress.state -eq 'done') { return }
        if ($progress.state -in @('failed','cancelled')) {
            throw "Generation ended with $($progress.state): $($progress.message)"
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Generation timeout'
}

function Start-GenerationExpectFailure([hashtable]$data, [string]$messagePattern) {
    $null = Invoke-RestMethod "$baseUrl/api/data" -Method Put -ContentType 'application/json' `
        -Body ($data | ConvertTo-Json -Depth 20)
    $regen = Invoke-RestMethod "$baseUrl/api/schedule/regenerate" -Method Post `
        -ContentType 'application/json' -Body '{}'
    if (!$regen.async) { throw 'Weekly generation did not start asynchronously' }

    for ($i=0; $i -lt 120; $i++) {
        $progress = Invoke-RestMethod "$baseUrl/api/schedule/progress"
        if ($progress.state -eq 'done') {
            throw "Expected generation failure matching '$messagePattern', but it completed"
        }
        if ($progress.state -eq 'cancelled') { throw 'Generation was unexpectedly cancelled' }
        if ($progress.state -eq 'failed') {
            if ($progress.message -notmatch $messagePattern) {
                throw "Unexpected failure message: $($progress.message)"
            }
            return $progress
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Expected generation failure timed out'
}

function Start-Generation([hashtable]$data) {
    $null = Invoke-RestMethod "$baseUrl/api/data" -Method Put -ContentType 'application/json' `
        -Body ($data | ConvertTo-Json -Depth 20)
    $regen = Invoke-RestMethod "$baseUrl/api/schedule/regenerate" -Method Post `
        -ContentType 'application/json' -Body '{}'
    if (!$regen.async) { throw 'Weekly generation did not start asynchronously' }
    Wait-Generation
    Invoke-RestMethod "$baseUrl/api/schedule"
}

try {
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'data') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'output/latest') -Force | Out-Null
    $testAuth = @{
        username = 'fake_grid'
        password_salt = '00112233445566778899aabbccddeeff'
        password_hash = 'b866e38c02150905dcadf54bf7eddfd833b09c541192b974ea5ea48bd64680d8'
        iterations = 150000
    } | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $testRoot 'data/auth_config.json'), $testAuth,
        (New-Object Text.UTF8Encoding($false)))

    $process = Start-Process -FilePath $SolverPath -ArgumentList @("$port") `
        -WorkingDirectory $testRoot -PassThru -WindowStyle Hidden
    $ready = $false
    for ($i=0; $i -lt 40; $i++) {
        try {
            $null = Invoke-RestMethod "$baseUrl/api/auth/status" -TimeoutSec 1
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    if (!$ready) { throw 'API did not start' }
    $webSession = New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $null = Invoke-RestMethod "$baseUrl/api/auth/login" -Method Post -ContentType 'application/json' `
        -WebSession $webSession -Body (@{username='fake_grid';password='fake-grid-pass'} | ConvertTo-Json)
    $PSDefaultParameterValues['Invoke-RestMethod:WebSession'] = $webSession

    # Regression 1: по три пары у двух физических подгрупп. В расписании шесть
    # событий, но hard max=3 должен применяться к каждой части отдельно.
    $parallelData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-03'
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=3; max_student_pairs_per_day=3
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
                max_whole_group_same_subject_pairs_per_day=2
                max_same_subject_pairs_per_day=3
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='PARALLEL';parts=2;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(0..5 | ForEach-Object {
            @{id=$_;uid="teacher-$_";name="Teacher $_";default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-WorkDays)}
        })
        rooms=@(
            @{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true},
            @{id=1;uid='room-1';name='102';campus=0;room_type=0;capacity=0;equipment=@();active=$true}
        )
        lessons=@(
            (New-Lesson 0 0 0 0), (New-Lesson 1 0 0 1), (New-Lesson 2 0 0 2),
            (New-Lesson 3 0 1 3), (New-Lesson 4 0 1 4), (New-Lesson 5 0 1 5)
        )
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    foreach ($lesson in $parallelData.lessons) {
        $lesson.name = 'Parallel Subject'
        $lesson.subject_id = 0
    }
    $parallelSchedule = Start-Generation $parallelData
    $parallelEvents = @($parallelSchedule.groups[0].days.slots.lessons | Where-Object { $_ })
    if ($parallelEvents.Count -ne 6) {
        throw "Expected six parallel subgroup events, got $($parallelEvents.Count)"
    }
    $part0Slots = @($parallelSchedule.groups[0].days.slots | Where-Object {
        @($_.lessons | Where-Object subgroup -eq 0).Count -gt 0
    })
    $part1Slots = @($parallelSchedule.groups[0].days.slots | Where-Object {
        @($_.lessons | Where-Object subgroup -eq 1).Count -gt 0
    })
    if ($part0Slots.Count -ne 3 -or $part1Slots.Count -ne 3) {
        throw "Student max must be per physical part: part0=$($part0Slots.Count), part1=$($part1Slots.Count)"
    }

    # Regression 2: три общегрупповые пары одного subject_id в один день
    # запрещены, даже если строки имеют разные названия и подгрупповый предел=3.
    # Это одновременно ловит обход лимита через name/is_lab и ошибочное применение
    # подгруппового послабления к занятиям всей группы.
    $wholeLessons = @(
        (New-Lesson 0 0 -1 0),
        (New-Lesson 1 0 -1 1),
        (New-Lesson 2 0 -1 2)
    )
    for ($i=0; $i -lt $wholeLessons.Count; $i++) {
        $wholeLessons[$i].name = "Same subject row $i"
        $wholeLessons[$i].subject_id = 42
    }
    $wholeSubjectData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-03'
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
                max_whole_group_same_subject_pairs_per_day=2
                max_same_subject_pairs_per_day=3
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='WHOLE-SUBJECT';parts=2;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(0..2 | ForEach-Object {
            @{id=$_;uid="teacher-$_";name="Teacher $_";default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-WorkDays)}
        })
        rooms=@(@{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true})
        lessons=$wholeLessons
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $null = Start-GenerationExpectFailure $wholeSubjectData 'INFEASIBLE'

    # Regression 3: period target fail-closed. Одной активной пары недостаточно,
    # если на весь выбранный диапазон явно требуется минимум две.
    $targetLesson = New-Lesson 0 0 -1 0
    $targetShortfallData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-03'
            teacher_period_targets=@(@{teacher=0;minimum_pairs=2})
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='TARGET-SHORTFALL';parts=1;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(@{id=0;uid='teacher-0';name='Teacher Target';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-WorkDays)})
        rooms=@(@{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true})
        lessons=@($targetLesson)
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $null = Start-GenerationExpectFailure $targetShortfallData '.+'
    $targetPreflight = Get-LatestCandidateReport 'solver_preflight.json'
    $targetIssues = @($targetPreflight.issues | Where-Object code -eq 'teacher_period_target_shortfall')
    if ($targetIssues.Count -ne 1 -or $targetIssues[0].scope -ne 'period') {
        throw "Teacher period target shortfall was not reported: $($targetPreflight | ConvertTo-Json -Depth 10)"
    }

    # Цель применяется один раз ко всему периоду, не к каждой неделе. Две пары,
    # распределённые по двум неделям 1+1, должны выполнить minimum_pairs=2.
    $periodLesson = New-Lesson 0 0 -1 0
    $periodLesson.total_hours = 4
    $periodLesson.total_slots = 2
    $targetPeriodData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-10'
            teacher_period_targets=@(@{teacher=0;minimum_pairs=2})
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='TARGET-PERIOD';parts=1;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(@{id=0;uid='teacher-0';name='Teacher Target';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-WorkDays)})
        rooms=@(@{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true})
        lessons=@($periodLesson)
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $targetPeriodSchedule = Start-Generation $targetPeriodData
    $targetPeriodEvents = @($targetPeriodSchedule.groups[0].days.slots.lessons | Where-Object { $_ })
    if ($targetPeriodEvents.Count -ne 2) {
        throw "Teacher period target was not fully scheduled: events=$($targetPeriodEvents.Count)"
    }

    # Regression 4: преподаватель с hard max=7 обязан уместить семь физических
    # пар в семь разных pair-slot одного дня; значение 7 не должно превращаться в 6.
    $sevenLessons = @()
    for ($id=0; $id -lt 7; $id++) { $sevenLessons += New-Lesson $id 0 -1 0 }
    $teacherData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-03'
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='SEVEN';parts=1;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(@{id=0;uid='teacher-0';name='Teacher Seven';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-WorkDays)})
        rooms=@(@{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true})
        lessons=$sevenLessons
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $teacherSchedule = Start-Generation $teacherData
    $teacherEvents = @($teacherSchedule.groups[0].days.slots.lessons | Where-Object { $_ })
    $occupiedSlots = @($teacherSchedule.groups[0].days.slots | Where-Object { @($_.lessons).Count -gt 0 })
    if ($teacherEvents.Count -ne 7 -or $occupiedSlots.Count -ne 7) {
        throw "Teacher max_pairs_per_day=7 was not enforced as seven physical slots: events=$($teacherEvents.Count), slots=$($occupiedSlots.Count)"
    }

    # Regression 5: два обязательных размещения при единственном физическом
    # slot должны завершиться QUOTA_INFEASIBLE, а не успешным балансом с missing.
    $quotaLesson = New-Lesson 0 0 -1 0
    $quotaLesson.total_hours = 4
    $quotaLesson.total_slots = 2
    $quotaData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-03'
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='QUOTA';parts=1;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-OneSlotWorkDays)})
        teachers=@(@{id=0;uid='teacher-0';name='Teacher Quota';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-OneSlotWorkDays)})
        rooms=@(@{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true})
        lessons=@($quotaLesson)
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $null = Start-GenerationExpectFailure $quotaData 'INFEASIBLE'
    $quotaReport = Get-LatestCandidateReport 'quota_balance.json'
    if ($quotaReport.success -or $quotaReport.status -notmatch 'INFEASIBLE') {
        throw "Missing quota was accepted as successful: $($quotaReport | ConvertTo-Json -Depth 8)"
    }

    # Regression 6: ПП ставится максимум по три пары в доступный день. Четвёртая
    # пара при одном дне обязана быть поймана финальным remaining_hours invariant.
    $ppLesson = New-Lesson 0 0 -1 0
    $ppLesson.total_hours = 8
    $ppLesson.total_slots = 4
    $ppLesson.is_pp = $true
    $ppData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-03'
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_max_two_same_subject_per_day=$true
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='PP';parts=1;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(@{id=0;uid='teacher-0';name='Teacher PP';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-WorkDays)})
        rooms=@(@{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true})
        lessons=@($ppLesson)
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $null = Start-GenerationExpectFailure $ppData '.+'
    $quality = Get-LatestCandidateReport 'quality_report.json'
    if ($quality.planned_hours -ne 8 -or $quality.scheduled_hours -ne 6 -or $quality.remaining_hours -ne 2) {
        throw "Final remaining-hours invariant report is invalid: $($quality | ConvertTo-Json -Depth 8)"
    }

    # Regression 7: подгруппа 0 может учиться только в четверг, подгруппа 1 —
    # только в пятницу. Раньше relaxed fallback снимал hard-синхронизацию дней и
    # возвращал success; теперь точная INFEASIBLE-модель должна остаться failure.
    $fallbackData = @{
        schema_version=4
        settings=@{
            start_date='2026-09-03'; end_date='2026-09-04'
            solver_config=@{
                week_time_limit_seconds=10; quality_improvement_seconds=0; solver_workers=2
                min_student_pairs_per_study_day=1; max_student_pairs_per_day=7
                hard_no_student_windows=$true; hard_no_teacher_windows=$true
                hard_max_two_same_subject_per_day=$true
            }
        }
        campuses=@(@{id=0;name='Campus A'},@{id=1;name='Campus B'})
        room_types=@()
        groups=@(@{id=0;uid='group-0';name='NO-FALLBACK';parts=2;size=0;home_campus=0;class_hour_enabled=$false;work_days=(New-WorkDays)})
        teachers=@(
            @{id=0;uid='teacher-0';name='Thursday Teacher';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-SingleWeekdayWorkDays 4)},
            @{id=1;uid='teacher-1';name='Friday Teacher';default_room=-1;campus_priority=@(0);max_pairs_per_day=7;work_days=(New-SingleWeekdayWorkDays 5)}
        )
        rooms=@(
            @{id=0;uid='room-0';name='101';campus=0;room_type=0;capacity=0;equipment=@();active=$true},
            @{id=1;uid='room-1';name='102';campus=0;room_type=0;capacity=0;equipment=@();active=$true}
        )
        lessons=@((New-Lesson 0 0 0 0),(New-Lesson 1 0 1 1))
        unavailable=@(); teacher_unavailable=@(); substitutions=@(); accounting_adjustments=@()
    }
    $null = Start-GenerationExpectFailure $fallbackData 'INFEASIBLE'
    $fallbackQuota = Get-LatestCandidateReport 'quota_balance.json'
    if (!$fallbackQuota.success) {
        throw "No-fallback scenario must reach the exact weekly model: $($fallbackQuota | ConvertTo-Json -Depth 8)"
    }

    Write-Host 'Solver P0 regression passed: subgroup 3+3, whole-group subject max 2, teacher period targets, teacher 7, strict quota, final load invariant and no relaxed fallback.'
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
