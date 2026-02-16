# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "5.3_Composer"
.\5.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause