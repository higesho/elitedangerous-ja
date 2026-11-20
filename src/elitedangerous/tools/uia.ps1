param([string]$title, [string]$name, [switch]$list)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $title)
$w = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
if (-not $w) { Write-Output "no window"; exit 1 }
$all = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
foreach ($e in $all) {
  $n = $e.Current.Name; $t = $e.Current.ControlType.ProgrammaticName
  if ($list) { if ($n) { Write-Output "$t | $n | $($e.Current.AutomationId)" } }
  elseif ($n -eq $name -or $e.Current.AutomationId -eq $name) {
    $p = $null
    if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); Write-Output "invoked $n"; exit 0 }
  }
}
