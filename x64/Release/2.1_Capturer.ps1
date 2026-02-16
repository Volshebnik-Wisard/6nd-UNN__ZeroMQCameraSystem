# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "2.1_Capturer"
.\2.1_Capturer.exe | Tee-Object -FilePath "Capturer.txt"
pause