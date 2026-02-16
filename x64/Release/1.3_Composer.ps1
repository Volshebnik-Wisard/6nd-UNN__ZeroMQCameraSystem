# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "1.3_Composer"
.\1.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause