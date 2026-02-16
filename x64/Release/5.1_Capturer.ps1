# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "5.1_Capturer"
.\5.1_Capturer.exe | Tee-Object -FilePath "Capturer.txt"
pause