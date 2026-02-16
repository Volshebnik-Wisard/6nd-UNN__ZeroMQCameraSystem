# Запуск Worker с логированием
$Host.UI.RawUI.WindowTitle = "6.2_Worker"
$worker_id = "Worker {0:HH-mm-ss-fff}" -f (Get-Date)
.\6.2_Worker.exe | Tee-Object -FilePath "$worker_id.txt"
pause