# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "2.3_Composer"
.\2.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause