# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "3.3_Composer"
.\3.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause