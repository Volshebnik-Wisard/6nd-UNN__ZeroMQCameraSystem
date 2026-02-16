# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "7.3_Composer"
.\7.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause