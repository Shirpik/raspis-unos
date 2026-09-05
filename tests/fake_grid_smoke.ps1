param([Parameter(Mandatory=$true)][string]$SolverPath)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("raspis-fake-grid-" + [guid]::NewGuid().ToString('N'))
$port = Get-Random -Minimum 18080 -Maximum 28080
$baseUrl = "http://127.0.0.1:$port"
$process = $null
try {
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'data') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'output/latest') -Force | Out-Null
    $data = @{
        schema_version = 4
        settings = @{
            start_date = '2026-09-07'; end_date = '2026-09-19'
            solver_config = @{ week_time_limit_seconds = 5; quality_improvement_seconds = 0.25; solver_workers = 2; hard_no_student_windows = $false; optimize_student_windows = $true; student_window_weight = 300 }
        }
        campuses = @(@{id=0;name='Lesnaya'},@{id=1;name='Krivousova 53'})
        room_types = @(
            @{id=1;name='Lecture room';description='Theory'},
            @{id=2;name='Workshop';description='Machines'},
            @{id=3;name='Computer room';description='PC'}
        )
        groups = @(@{id=0;name='TEST-101';parts=2;size=20;home_campus=0})
        teachers = @(@{id=0;name='Test Teacher';default_room=0;campus_priority=@(0,1)},@{id=1;name='Substitute Teacher'})
        rooms = @(@{id=0;name='101';campus=0;room_type=1;capacity=25;equipment=@('projector');active=$true})
        unavailable = @(); teacher_unavailable = @()
        lessons = @(
            @{id=0;group=0;subgroup=-1;teacher=0;total_hours=4;total_slots=2;name='Mathematics';subject_id=0;is_lab=$false;is_block=$false;is_pp=$false;allowed_campuses=@(0);week_parity='all';fixed_room=0;allow_room_substitution=$true;required_room_type=1;required_capacity=20;required_equipment=@('projector')},
            @{id=1;group=0;subgroup=-1;teacher=0;total_hours=4;total_slots=2;name='Physics';subject_id=1;is_lab=$false;is_block=$false;is_pp=$false;allowed_campuses=@(0);week_parity='all';fixed_room=0;allow_room_substitution=$true;required_room_type=1;required_capacity=20;required_equipment=@()}
        )
    }
    $json = $data | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText((Join-Path $testRoot 'data/timetable_data.json'), $json, (New-Object Text.UTF8Encoding($false)))
    $testAuth = @{
        username = 'fake_grid'
        password_salt = '00112233445566778899aabbccddeeff'
        password_hash = 'b866e38c02150905dcadf54bf7eddfd833b09c541192b974ea5ea48bd64680d8'
        iterations = 150000
    } | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $testRoot 'data/auth_config.json'), $testAuth, (New-Object Text.UTF8Encoding($false)))
    $process = Start-Process -FilePath $SolverPath -ArgumentList @("$port") -WorkingDirectory $testRoot -PassThru -WindowStyle Hidden
    $ready = $false
    for ($i=0; $i -lt 40; $i++) {
        try { $null = Invoke-RestMethod "$baseUrl/api/auth/status" -TimeoutSec 1; $ready=$true; break } catch { Start-Sleep -Milliseconds 250 }
    }
    if (!$ready) { throw 'API did not start' }
    $webSession = New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $null = Invoke-RestMethod "$baseUrl/api/auth/login" -Method Post -ContentType 'application/json' -WebSession $webSession -Body (@{username='fake_grid';password='fake-grid-pass'} | ConvertTo-Json)
    $PSDefaultParameterValues['Invoke-RestMethod:WebSession'] = $webSession
    $PSDefaultParameterValues['Invoke-WebRequest:WebSession'] = $webSession
    $workDays = 1..7 | ForEach-Object { @{day=$_;enabled=($_ -le 6);start_slot=2;end_slot=4} }
    $bulkGroup = Invoke-RestMethod "$baseUrl/api/groups/bulk" -Method Patch -ContentType 'application/json' -Body (@{all=$true;patch=@{work_period=@{from='2026-09-07';to='2026-09-19'};work_days=$workDays}} | ConvertTo-Json -Depth 8)
    if ($bulkGroup.data.updated -ne 1) { throw 'Group bulk working time update failed' }
    $bulkTeacher = Invoke-RestMethod "$baseUrl/api/teachers/bulk" -Method Patch -ContentType 'application/json' -Body (@{ids=@(0);patch=@{work_period=@{from='2026-09-07';to='2026-09-19'};work_days=$workDays}} | ConvertTo-Json -Depth 8)
    if ($bulkTeacher.data.updated -ne 1) { throw 'Teacher bulk working time update failed' }
    $audit = Invoke-RestMethod "$baseUrl/api/audit"
    if (!$audit.ok) { throw "Fake grid audit failed: $($audit | ConvertTo-Json -Depth 8)" }
    $profiles = Invoke-RestMethod "$baseUrl/api/settings/solver-profiles"
    if ($profiles.Count -ne 3) { throw 'Solver profiles endpoint is invalid' }
    $profileResult = Invoke-RestMethod "$baseUrl/api/settings/solver-profile/fast" -Method Post -ContentType 'application/json' -Body '{}'
    if ($profileResult.data.profile -ne 'fast' -or $profileResult.data.quality_improvement_seconds -ne 0) {
        throw 'Fast solver profile was not applied'
    }
    $regen = Invoke-RestMethod "$baseUrl/api/schedule/regenerate" -Method Post -ContentType 'application/json' -Body '{}'
    if (!$regen.async) { throw 'Weekly generation did not start asynchronously' }
    $done = $false
    for ($i=0; $i -lt 100; $i++) {
        $progress = Invoke-RestMethod "$baseUrl/api/schedule/progress"
        if ($progress.state -eq 'done') { $done=$true; break }
        if ($progress.state -in @('failed','cancelled')) {
            $validationFile = Get-ChildItem (Join-Path $testRoot 'output/candidates') -Filter validation_report.json -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            $details = if ($validationFile) { Get-Content $validationFile.FullName -Raw } else { '' }
            throw "Generation ended with $($progress.state): $($progress.message)`n$details"
        }
        Start-Sleep -Milliseconds 250
    }
    if (!$done) { throw 'Generation timeout on fake grid' }
    $schedule = Invoke-RestMethod "$baseUrl/api/schedule"
    if ($schedule.groups.Count -ne 1) { throw 'Generated schedule does not contain the fake group' }
    $validation = Invoke-RestMethod "$baseUrl/api/schedule/validate" -Method Post `
        -ContentType 'application/json' -Body (@{source='auto'} | ConvertTo-Json)
    if (!$validation.ok -or $validation.summary.hard_errors -ne 0) {
        throw "Generated schedule did not pass the independent validator: $($validation | ConvertTo-Json -Depth 8)"
    }
    if (@($validation.categories).Count -lt 10) { throw 'Validation category coverage is incomplete' }

    $broken = $schedule | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $occupied = $broken.groups[0].days | ForEach-Object { $_.slots } | Where-Object { @($_.lessons).Count -gt 0 } | Select-Object -First 1
    $occupied.lessons = @($occupied.lessons) + @($occupied.lessons[0])
    $brokenValidation = Invoke-RestMethod "$baseUrl/api/schedule/validate" -Method Post `
        -ContentType 'application/json' -Body (@{source='payload';schedule=$broken} | ConvertTo-Json -Depth 25)
    if ($brokenValidation.ok -or 'teacher_conflict' -notin @($brokenValidation.issues.code)) {
        throw 'Payload validator did not detect a teacher conflict'
    }

    try {
        Invoke-RestMethod "$baseUrl/api/settings/solver-config" -Method Patch `
            -ContentType 'application/json' -Body (@{unknown_parameter=1} | ConvertTo-Json) | Out-Null
        throw 'Unknown solver parameter was silently accepted'
    } catch {
        if ($_.Exception.Response.StatusCode.value__ -ne 400) { throw }
    }
    $scheduledLessons = @($schedule.groups[0].days | ForEach-Object { $_.slots } | ForEach-Object { $_.lessons } | Where-Object { $_ })
    if ($scheduledLessons.Count -ne 4 -or @($scheduledLessons | Where-Object { $_.room_id -ne 0 }).Count -ne 0) {
        throw 'Automatic room allocation was not written into schedule JSON'
    }
    $occupiedSlots = @($schedule.groups[0].days | ForEach-Object { $_.slots } | Where-Object { @($_.lessons).Count -gt 0 })
    if (@($occupiedSlots | Where-Object { $_.slot -lt 2 -or $_.slot -gt 4 }).Count -ne 0) {
        throw 'Working-time slot restriction was not enforced'
    }
    $roomReport = Invoke-RestMethod "$baseUrl/api/schedule/rooms"
    if (!$roomReport.inventory_configured -or $roomReport.assigned -ne 4 -or $roomReport.unassigned -ne 0) {
        throw "Room allocation report is invalid: $($roomReport | ConvertTo-Json -Depth 8)"
    }
    $quality = Invoke-RestMethod "$baseUrl/api/schedule/quality"
    if ($quality.planned_hours -ne 8 -or $quality.scheduled_hours -ne 8 -or $quality.remaining_hours -ne 0) {
        throw "Quality report hours are invalid: $($quality | ConvertTo-Json -Depth 8)"
    }
    $metrics = Invoke-RestMethod "$baseUrl/api/schedule/solver-metrics"
    if ($metrics.weeks.Count -ne 2 -or $metrics.weeks[1].warm_start_hints -le 0) {
        throw "Warm-start solver metrics are invalid: $($metrics | ConvertTo-Json -Depth 8)"
    }
    $preflight = Invoke-RestMethod "$baseUrl/api/schedule/preflight"
    if (!$preflight.ok -or $preflight.summary.errors -ne 0) {
        throw "Weekly preflight report is invalid: $($preflight | ConvertTo-Json -Depth 8)"
    }
    $quotaBalance = Invoke-RestMethod "$baseUrl/api/schedule/quota-balance"
    if (!$quotaBalance.success) {
        throw "Quota balancing failed: $($quotaBalance | ConvertTo-Json -Depth 8)"
    }
    $hours = Invoke-RestMethod "$baseUrl/api/hours"
    if (!$hours.schedule_found -or $hours.groups.Count -ne 1) { throw 'Hours report was not built' }
    if ($hours.weeks.Count -ne 2 -or $hours.weeks[0].from -ne '2026-09-07' -or $hours.weeks[1].to -ne '2026-09-19') {
        throw "Semester week grid is invalid: $($hours.weeks | ConvertTo-Json -Depth 4)"
    }
    if ($hours.lessons[0].subject_id -ne 0 -or $hours.lessons[0].subgroup -ne -1) {
        throw 'Lesson accounting metadata is missing'
    }
    $teacherHours = $hours.teachers | Where-Object teacher_id -eq 0
    if (@($teacherHours.scheduled_occurrences).Count -ne 4 -or
        @($teacherHours.scheduled_occurrences | Where-Object { !$_.date -or $_.slot -lt 2 -or $_.slot -gt 4 }).Count -ne 0) {
        throw "Scheduled occurrence dates are invalid: $($teacherHours | ConvertTo-Json -Depth 8)"
    }

    # A short-period quota is total_slots; semester total_hours must not make a
    # complete generated variant fail the final API gate.
    $lessonsForGate = Invoke-RestMethod "$baseUrl/api/lessons"
    $lessonForGate = $lessonsForGate[0]
    $originalHours = if ($null -ne $lessonForGate.total_hours) { $lessonForGate.total_hours } else { $lessonForGate.total_slots * 2 }
    $lessonForGate | Add-Member -NotePropertyName total_hours -NotePropertyValue ($originalHours + 10000) -Force
    $null = Invoke-RestMethod "$baseUrl/api/lessons/$($lessonForGate.id)" -Method Put -ContentType 'application/json' -Body ($lessonForGate | ConvertTo-Json -Depth 15)
    $null = Invoke-RestMethod "$baseUrl/api/schedule/regenerate" -Method Post -ContentType 'application/json' -Body '{}'
    $shortQuotaAccepted = $false
    for ($i=0; $i -lt 100; $i++) {
        $validationProgress = Invoke-RestMethod "$baseUrl/api/schedule/progress"
        if ($validationProgress.state -eq 'done') {
            if (!$validationProgress.validation.checked -or !$validationProgress.validation.ok -or
                $validationProgress.validation.remaining_hours -ne 0 -or
                $validationProgress.validation.hours_source -ne 'quality_report') {
                throw "Short quota validation returned invalid diagnostics: $($validationProgress | ConvertTo-Json -Depth 8)"
            }
            $shortQuotaAccepted = $true
            break
        }
        if ($validationProgress.state -in @('failed','cancelled')) {
            throw "Semester total_hours incorrectly rejected a short quota: $($validationProgress | ConvertTo-Json -Depth 8)"
        }
        Start-Sleep -Milliseconds 250
    }
    if (!$shortQuotaAccepted) { throw 'Short quota validation timeout' }
    $lessonForGate | Add-Member -NotePropertyName total_hours -NotePropertyValue $originalHours -Force
    $null = Invoke-RestMethod "$baseUrl/api/lessons/$($lessonForGate.id)" -Method Put -ContentType 'application/json' -Body ($lessonForGate | ConvertTo-Json -Depth 15)

    # Publication independently rejects a quality report with a positive remainder.
    $qualityReportPath = Join-Path $testRoot 'output/latest/quality_report.json'
    $savedQualityReport = [IO.File]::ReadAllText($qualityReportPath)
    $invalidQualityReport = $savedQualityReport | ConvertFrom-Json
    $invalidQualityReport.remaining_hours = 2
    [IO.File]::WriteAllText($qualityReportPath, ($invalidQualityReport | ConvertTo-Json -Depth 15), (New-Object Text.UTF8Encoding($false)))
    $hoursPublishBlocked = $false
    try {
        $null = Invoke-RestMethod "$baseUrl/api/schedule/publish" -Method Post -ContentType 'application/json' -Body '{}'
    } catch {
        $statusCode = [int]$_.Exception.Response.StatusCode
        $errorBody = $_.ErrorDetails.Message | ConvertFrom-Json
        $hoursPublishBlocked = $statusCode -eq 409 -and $errorBody.data.remaining_hours -eq 2
    } finally {
        [IO.File]::WriteAllText($qualityReportPath, $savedQualityReport, (New-Object Text.UTF8Encoding($false)))
    }
    if (!$hoursPublishBlocked) { throw 'Schedule with remaining hours was published' }

    # Равная агрегатная сумма тоже не даёт права публиковать вариант, если
    # quality report сообщает лишние размещения/расхождение отдельных уроков.
    $invalidExactReport = $savedQualityReport | ConvertFrom-Json
    $invalidExactReport.remaining_hours = 0
    $invalidExactReport.excess_hours = 2
    $invalidExactReport.mismatched_lessons = 1
    $invalidExactReport.load_matches_plan_exactly = $false
    [IO.File]::WriteAllText($qualityReportPath, ($invalidExactReport | ConvertTo-Json -Depth 15), (New-Object Text.UTF8Encoding($false)))
    $exactPublishBlocked = $false
    try {
        $null = Invoke-RestMethod "$baseUrl/api/schedule/publish" -Method Post -ContentType 'application/json' -Body '{}'
    } catch {
        $statusCode = [int]$_.Exception.Response.StatusCode
        $errorBody = $_.ErrorDetails.Message | ConvertFrom-Json
        $exactPublishBlocked = $statusCode -eq 409 -and
            $errorBody.data.excess_hours -eq 2 -and
            $errorBody.data.mismatched_lessons -eq 1
    } finally {
        [IO.File]::WriteAllText($qualityReportPath, $savedQualityReport, (New-Object Text.UTF8Encoding($false)))
    }
    if (!$exactPublishBlocked) { throw 'Schedule with compensated per-lesson mismatch was published' }

    $firstDay = $schedule.groups[0].days | Where-Object { @($_.slots.lessons).Count -gt 0 } | Select-Object -First 1
    $firstSlot = $firstDay.slots | Where-Object { @($_.lessons).Count -gt 0 } | Select-Object -First 1
    $firstLesson = $firstSlot.lessons | Select-Object -First 1
    $substitution = @{lesson_id=$firstLesson.id;date=$firstDay.date_iso;slot=$firstSlot.slot;absent_teacher=0;substitute_teacher=1;hours=2;reason='sick-leave';status='active'} | ConvertTo-Json
    $null = Invoke-RestMethod "$baseUrl/api/substitutions" -Method Post -ContentType 'application/json' -Body $substitution
    $hoursAfter = Invoke-RestMethod "$baseUrl/api/hours"
    $original = $hoursAfter.teachers | Where-Object teacher_id -eq 0
    $substitute = $hoursAfter.teachers | Where-Object teacher_id -eq 1
    if ($original.credited_hours -ne 6 -or $original.substitution_out_hours -ne 2 -or $substitute.credited_hours -ne 2 -or $substitute.substitution_in_hours -ne 2) {
        throw "Substitution accounting is invalid: $($hoursAfter.teachers | ConvertTo-Json -Depth 8)"
    }
    if (@($original.scheduled_occurrences).Count -ne 4 -or @($original.credited_occurrences).Count -ne 3 -or
        @($substitute.credited_occurrences | Where-Object { $_.is_substitution -and $_.actual_teacher_id -eq 1 }).Count -ne 1) {
        throw "Occurrence accounting after substitution is invalid: $($hoursAfter.teachers | ConvertTo-Json -Depth 10)"
    }
    $occupancy = Invoke-RestMethod "$baseUrl/api/accounting/teacher-occupancy"
    if (@($occupancy.entries | Where-Object { $_.is_substitution -and $_.teacher_id -eq 1 }).Count -ne 1) {
        throw 'Teacher occupancy did not apply substitution'
    }
    $csv = (Invoke-WebRequest "$baseUrl/api/accounting/substitutions.csv" -UseBasicParsing).Content
    if ($csv -notmatch 'sick-leave' -or $csv -notmatch 'Substitute Teacher') { throw 'Substitution CSV is invalid' }
    $null = Invoke-RestMethod "$baseUrl/api/schedule/publish" -Method Post -ContentType 'application/json' -Body '{}'
    $published = Invoke-RestMethod "$baseUrl/api/schedule/published"
    if ($published.groups.Count -ne 1) { throw 'Published student schedule is invalid' }

    # Publication is independently gated by the room-allocation report.
    $roomReportPath = Join-Path $testRoot 'output/latest/room_allocation.json'
    $savedRoomReport = [IO.File]::ReadAllText($roomReportPath)
    $invalidRoomReport = $savedRoomReport | ConvertFrom-Json
    $invalidRoomReport.unassigned = 1
    [IO.File]::WriteAllText($roomReportPath, ($invalidRoomReport | ConvertTo-Json -Depth 15), (New-Object Text.UTF8Encoding($false)))
    $roomGateBlocked = $false
    try {
        $null = Invoke-RestMethod "$baseUrl/api/schedule/publish" -Method Post -ContentType 'application/json' -Body '{}'
    } catch {
        $statusCode = [int]$_.Exception.Response.StatusCode
        $errorBody = $_.ErrorDetails.Message | ConvertFrom-Json
        $roomGateBlocked = $statusCode -eq 409 -and $errorBody.data.unassigned_rooms -eq 1
    } finally {
        [IO.File]::WriteAllText($roomReportPath, $savedRoomReport, (New-Object Text.UTF8Encoding($false)))
    }
    if (!$roomGateBlocked) { throw 'Schedule with an unassigned room was published' }

    $roomTypes = Invoke-RestMethod "$baseUrl/api/room-types"
    if ($roomTypes.Count -ne 3 -or ($roomTypes | Where-Object id -eq 2).name -ne 'Workshop') {
        throw 'Room type catalog endpoint is invalid'
    }

    # Zero class hour must continue immediately into the first regular pair.
    $oneSlotDays = 1..7 | ForEach-Object { @{day=$_;enabled=($_ -eq 1);start_slot=1;end_slot=1} }
    $conflictData = @{
        schema_version = 4
        settings = @{start_date='2026-09-07';end_date='2026-09-07';solver_config=@{week_time_limit_seconds=5;quality_improvement_seconds=0;solver_workers=2}}
        campuses = @(@{id=0;name='Lesnaya'},@{id=1;name='Krivousova 53'})
        room_types = $roomTypes
        groups = @(
            @{id=0;name='ROOM-A';parts=1;size=20;home_campus=0;curator_teacher=0;class_hour_enabled=$true;class_hour_campus=-1;work_period=@{from='2026-09-07';to='2026-09-07'};work_days=$oneSlotDays},
            @{id=1;name='ROOM-B';parts=1;size=20;home_campus=0;curator_teacher=1;class_hour_enabled=$true;class_hour_campus=0;work_period=@{from='2026-09-07';to='2026-09-07'};work_days=$oneSlotDays}
        )
        teachers = @(
            @{id=0;name='Teacher A';default_room=0;campus_priority=@(0);work_period=@{from='2026-09-07';to='2026-09-07'};work_days=$oneSlotDays},
            @{id=1;name='Teacher B';default_room=0;campus_priority=@(0);work_period=@{from='2026-09-07';to='2026-09-07'};work_days=$oneSlotDays}
        )
        rooms = @(
            @{id=0;name='101';campus=0;room_type=1;capacity=25;equipment=@();active=$true},
            @{id=1;name='102';campus=0;room_type=1;capacity=25;equipment=@();active=$true},
            @{id=2;name='PC-1';campus=0;room_type=3;capacity=25;equipment=@('PC');active=$true}
        )
        unavailable=@();teacher_unavailable=@();substitutions=@();accounting_adjustments=@()
        lessons = @(
            @{id=0;group=0;subgroup=-1;teacher=0;total_hours=2;total_slots=1;name='Theory A';subject_id=0;is_lab=$false;is_block=$false;allowed_campuses=@(0);week_parity='all';fixed_room=0;allow_room_substitution=$true;required_room_type=1;required_capacity=20;required_equipment=@()},
            @{id=1;group=1;subgroup=-1;teacher=1;total_hours=2;total_slots=1;name='Theory B';subject_id=1;is_lab=$false;is_block=$false;allowed_campuses=@(0);week_parity='all';fixed_room=0;allow_room_substitution=$true;required_room_type=1;required_capacity=20;required_equipment=@()}
        )
    }
    $null = Invoke-RestMethod "$baseUrl/api/data" -Method Put -ContentType 'application/json' -Body ($conflictData | ConvertTo-Json -Depth 15)
    $null = Invoke-RestMethod "$baseUrl/api/schedule/regenerate" -Method Post -ContentType 'application/json' -Body '{}'
    $done = $false
    for ($i=0; $i -lt 80; $i++) {
        $progress = Invoke-RestMethod "$baseUrl/api/schedule/progress"
        if ($progress.state -eq 'done') { $done=$true; break }
        if ($progress.state -in @('failed','cancelled')) { throw "Room conflict generation ended with $($progress.state): $($progress.message)" }
        Start-Sleep -Milliseconds 250
    }
    if (!$done) { throw 'Room conflict generation timeout' }
    $replacementReport = Invoke-RestMethod "$baseUrl/api/schedule/rooms"
    if ($replacementReport.assigned -ne 2 -or $replacementReport.unassigned -ne 0 -or $replacementReport.substituted -ne 1 -or @($replacementReport.substitutions).Count -ne 1) {
        throw "Automatic room replacement is invalid: $($replacementReport | ConvertTo-Json -Depth 10)"
    }
    $change = @($replacementReport.substitutions)[0]
    if ($change.requested_room_id -ne 0 -or $change.assigned_room_id -ne 1 -or $change.room_type -ne 1 -or [string]::IsNullOrWhiteSpace($change.reason)) {
        throw "Automatic room replacement details are invalid: $($change | ConvertTo-Json -Depth 8)"
    }
    $replacementSchedule = Invoke-RestMethod "$baseUrl/api/schedule"
    $ordinarySlots = @($replacementSchedule.groups.days.slots | Where-Object { $_.slot -gt 0 -and $_.lessons.Count -gt 0 })
    if ($ordinarySlots.Count -ne 2 -or @($ordinarySlots | Where-Object { $_.slot -ne 1 }).Count -gt 0) {
        throw 'Ordinary lessons must immediately follow zero class hours'
    }
    $zeroSlots = @($replacementSchedule.groups.days.slots | Where-Object { $_.slot -eq 0 -and $_.lessons[0].is_class_hour })
    if ($zeroSlots.Count -ne 2 -or @($zeroSlots | Where-Object { $_.time -match '08:15-08:55' }).Count -ne 2) {
        throw "Fixed Monday class hours are missing: $($zeroSlots | ConvertTo-Json -Depth 8)"
    }
    if (@($zeroSlots | Where-Object { $null -eq $_.lessons[0].room_id }).Count -ne 0) {
        throw 'Class hours must have actual rooms, not decorative entries'
    }
    $replacedLessons = @($replacementSchedule.groups.days.slots.lessons | Where-Object { $_.room_substituted })
    if ($replacedLessons.Count -ne 1 -or $replacedLessons[0].requested_room_id -ne 0 -or $replacedLessons[0].room_id -ne 1) {
        throw "Room replacement metadata is missing from schedule: $($replacedLessons | ConvertTo-Json -Depth 8)"
    }
    Write-Host 'Fake grid: audit, weekly solver, room types, automatic room replacement, quality, hours and publication passed.'
} finally {
    if ($process -and !$process.HasExited) {
        Stop-Process -Id $process.Id -Force
        if (!$process.WaitForExit(5000)) { throw 'Test API did not exit; temporary directory retained' }
    }
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
        $expectedPrefix = [IO.Path]::Combine([IO.Path]::GetTempPath(), 'raspis-fake-grid-')
        if (!$resolvedTestRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing cleanup outside the test-specific temporary directory'
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
