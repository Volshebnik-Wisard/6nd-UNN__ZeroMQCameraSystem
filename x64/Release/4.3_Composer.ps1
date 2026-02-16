# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "4.3_Composer"
.\4.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause