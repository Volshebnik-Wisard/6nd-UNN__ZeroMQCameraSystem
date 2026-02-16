# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "1.1_Capturer"
.\1.1_Capturer.exe | Tee-Object -FilePath "Capturer.txt"
pause