param(
    [Parameter(Mandatory=$true)][string]$NodePath,
    [Parameter(Mandatory=$true)][string]$ServerPath
)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("raspis-site-transfer-" + [guid]::NewGuid().ToString('N'))
$port = Get-Random -Minimum 38081 -Maximum 48080
$baseUrl = "http://127.0.0.1:$port"
$process = $null
$previousPort = $env:PORT
try {
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'data') -Force | Out-Null
    [IO.File]::Copy($ServerPath, (Join-Path $testRoot 'server.mjs'))
    $data = @{
        schema_version=4;settings=@{start_date='2026-09-07';end_date='2026-09-12'}
        campuses=@(@{id=0;name='Campus A'});room_types=@(@{id=1;name='Classroom'})
        groups=@(@{id=0;name='WEB-101';size=20});teachers=@(@{id=0;name='Web Teacher'},@{id=1;name='Web Substitute'})
        rooms=@(@{id=0;name='101';campus=0;room_type=1;capacity=25;active=$true})
        lessons=@(@{id=0;group=0;teacher=0;subgroup=-1;name='Web Subject';total_hours=2;required_room_type=1})
        unavailable=@();teacher_unavailable=@();substitutions=@(@{id=0;lesson_id=0;date='2026-09-07';slot=1;absent_teacher=0;substitute_teacher=1;hours=2;status='active'});accounting_adjustments=@(@{id=0;teacher_id=1;hours=1})
    }
    $auto = @{groups=@(@{group_index=0;group_name='WEB-101';days=@(@{date='07.09.2026';date_iso='2026-09-07';slots=@(@{slot=1;lessons=@(@{id=0;name='Web Auto';teacher_id=0})})})})}
    $manual = $auto | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $manual.groups[0].days[0].slots[0].lessons[0].name='Web Manual'
    $auth = @{username='fake_grid';password_salt='00112233445566778899aabbccddeeff';password_hash='b866e38c02150905dcadf54bf7eddfd833b09c541192b974ea5ea48bd64680d8';iterations=150000}
    foreach ($item in @(
        @('timetable_data.json',$data),@('schedule_all.json',$auto),@('published_schedule.json',$auto),
        @('manual_schedule.json',$manual),@('room_allocation.json',@{assigned=1;unassigned=0}),@('auth_config.json',$auth)
    )) {
        [IO.File]::WriteAllText((Join-Path $testRoot "data/$($item[0])"), ($item[1] | ConvertTo-Json -Depth 30), (New-Object Text.UTF8Encoding($false)))
    }
    $env:PORT="$port"
    $process=Start-Process -FilePath $NodePath -ArgumentList @((Join-Path $testRoot 'server.mjs')) -WorkingDirectory $testRoot -PassThru -WindowStyle Hidden
    $ready=$false
    for($i=0;$i -lt 40;$i++){try{$null=Invoke-RestMethod "$baseUrl/api/auth/status" -TimeoutSec 1;$ready=$true;break}catch{Start-Sleep -Milliseconds 250}}
    if(!$ready){throw 'Site API did not start'}
    $session=New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $null=Invoke-RestMethod "$baseUrl/api/auth/login" -Method Post -ContentType 'application/json' -WebSession $session -Body (@{username='fake_grid';password='fake-grid-pass'}|ConvertTo-Json)
    $PSDefaultParameterValues['Invoke-RestMethod:WebSession']=$session
    $bundle=Invoke-RestMethod "$baseUrl/api/transfer/export"
    if($bundle.summary.groups -ne 1 -or !$bundle.schedules.manual -or $bundle.data.accounting_adjustments.Count -ne 1){throw 'Site export is incomplete'}
    $null=Invoke-RestMethod "$baseUrl/api/groups/0" -Method Patch -ContentType 'application/json' -Body (@{name='BROKEN-WEB'}|ConvertTo-Json)
    $result=Invoke-RestMethod "$baseUrl/api/transfer/import" -Method Post -ContentType 'application/json' -Body (@{bundle=$bundle;primary_schedule='manual';publish=$true}|ConvertTo-Json -Depth 40)
    if(!$result.success){throw 'Site import failed'}
    $restored=Invoke-RestMethod "$baseUrl/api/data"
    $active=Invoke-RestMethod "$baseUrl/api/schedule"
    $published=Invoke-RestMethod "$baseUrl/api/schedule/published"
    $hours=Invoke-RestMethod "$baseUrl/api/hours"
    if($restored.groups[0].name -ne 'WEB-101' -or $restored.substitutions.Count -ne 1){throw 'Site data was not restored'}
    if($active.groups[0].days[0].slots[0].lessons[0].name -ne 'Web Manual' -or $published.groups[0].days[0].slots[0].lessons[0].name -ne 'Web Manual'){throw 'Site did not activate and publish manual schedule'}
    if(!$hours.schedule_found -or $hours.lessons[0].scheduled_hours -ne 2){throw 'Site did not recalculate accounting'}
    if(@(Get-ChildItem -File (Join-Path $testRoot 'data/transfer-backups') -Filter '*.raspis.json').Count -ne 1){throw 'Site backup was not created'}
    Write-Host 'Hosted site transfer: export, import, manual publication, accounting and backup passed.'
} finally {
    if($process -and !$process.HasExited){Stop-Process -Id $process.Id -Force}
    $env:PORT=$previousPort
    $resolvedTemp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath());$resolvedTest=[IO.Path]::GetFullPath($testRoot)
    if($resolvedTest.StartsWith($resolvedTemp,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolvedTest).StartsWith('raspis-site-transfer-',[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedTest)){Remove-Item -LiteralPath $resolvedTest -Recurse -Force}
}
