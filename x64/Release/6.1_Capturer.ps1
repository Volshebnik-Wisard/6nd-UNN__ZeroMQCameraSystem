# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "6.1_Capturer"
.\6.1_Capturer.exe | Tee-Object -FilePath "Capturer.txt"
pause