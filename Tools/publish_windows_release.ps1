param([string]$Version='v0.3.0',[switch]$Inspect)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$repo='XinluLi-weeder/AlternativeControll_Workshop'
$base="https://api.github.com/repos/$repo"
$credentialLines="protocol=https`nhost=github.com`n`n" | git -C $root credential fill
if($LASTEXITCODE -ne 0){throw 'GitHub credentials unavailable'}
$username=($credentialLines | Where-Object {$_ -like 'username=*'} | Select-Object -First 1).Substring(9)
$password=($credentialLines | Where-Object {$_ -like 'password=*'} | Select-Object -First 1).Substring(9)
$client=[System.Net.Http.HttpClient]::new()
$client.Timeout=[TimeSpan]::FromMinutes(60)
$client.DefaultRequestHeaders.UserAgent.ParseAdd('RoomShiftDemo-release/0.3')
$client.DefaultRequestHeaders.Accept.ParseAdd('application/vnd.github+json')
$client.DefaultRequestHeaders.Add('X-GitHub-Api-Version','2022-11-28')
$basic=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("${username}:${password}"))
$client.DefaultRequestHeaders.Authorization=[System.Net.Http.Headers.AuthenticationHeaderValue]::new('Basic',$basic)
function JsonRequest($method,$url,$body){
    $request=[System.Net.Http.HttpRequestMessage]::new($method,$url)
    if($null -ne $body){$request.Content=[System.Net.Http.StringContent]::new(($body | ConvertTo-Json -Depth 8),[Text.Encoding]::UTF8,'application/json')}
    try{
        $response=$client.SendAsync($request).GetAwaiter().GetResult()
        $content=$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if(-not $response.IsSuccessStatusCode){throw "GitHub HTTP $([int]$response.StatusCode): $content"}
        return ($content | ConvertFrom-Json)
    }finally{$request.Dispose()}
}
try{
    if($Inspect){
        $releases=JsonRequest 'GET' "$base/releases?per_page=15" $null
        $releases | Select-Object tag_name,draft,html_url,@{n='assets';e={($_.assets.name -join ', ')}}
        return
    }
    $assetPath=Join-Path $root "Releases/RoomShiftDemo-Windows-UE5.7-$Version.zip"
    if(-not (Test-Path -LiteralPath $assetPath)){throw "Missing ZIP: $assetPath"}
    $sha=(git -c "safe.directory=$root" -C $root rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0){throw 'Cannot resolve release commit'}
    $check=$client.GetAsync("$base/releases/tags/$Version").GetAwaiter().GetResult()
    if($check.StatusCode -eq [System.Net.HttpStatusCode]::NotFound){
        $release=JsonRequest 'POST' "$base/releases" @{
            tag_name=$Version;target_commitish=$sha;name="Room Shift $Version - Bedroom Windows Playtest"
            body=[IO.File]::ReadAllText((Join-Path $root "Documentation/ReleaseNotes-$Version.md"))
            draft=$true;prerelease=$true
        }
    }elseif($check.IsSuccessStatusCode){
        $release=$check.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
        if(-not $release.draft){throw 'This version is already published; refusing to overwrite it.'}
    }else{throw "Release lookup HTTP $([int]$check.StatusCode)"}
    $assetName=[IO.Path]::GetFileName($assetPath)
    if(@($release.assets | Where-Object {$_.name -eq $assetName}).Count){throw 'Asset already exists; refusing to replace it.'}
    $url="https://uploads.github.com/repos/$repo/releases/$($release.id)/assets?name=$([Uri]::EscapeDataString($assetName))"
    $stream=[IO.File]::OpenRead($assetPath)
    $request=[System.Net.Http.HttpRequestMessage]::new('POST',$url)
    $request.Content=[System.Net.Http.StreamContent]::new($stream)
    $request.Content.Headers.ContentType=[System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/zip')
    try{
        $response=$client.SendAsync($request).GetAwaiter().GetResult()
        $content=$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if(-not $response.IsSuccessStatusCode){throw "Upload HTTP $([int]$response.StatusCode): $content"}
        $asset=$content | ConvertFrom-Json
    }finally{$request.Dispose();$stream.Dispose()}
    if($asset.size -ne (Get-Item -LiteralPath $assetPath).Length){throw 'Uploaded size does not match local ZIP'}
    $release=JsonRequest 'PATCH' "$base/releases/$($release.id)" @{draft=$false}
    [pscustomobject]@{Release=$release.html_url;Download=$asset.browser_download_url;Bytes=$asset.size;Commit=$sha}
}finally{$client.Dispose()}
