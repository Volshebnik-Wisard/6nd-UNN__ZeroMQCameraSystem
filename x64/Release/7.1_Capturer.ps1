# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "7.1_Capturer"
.\7.1_Capturer.exe | Tee-Object -FilePath "Capturer.txt"
pause