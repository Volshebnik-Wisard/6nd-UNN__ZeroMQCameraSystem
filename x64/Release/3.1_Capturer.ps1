# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "3.1_Capturer"
.\3.1_Capturer.exe | Tee-Object -FilePath "Capturer.txt"
pause