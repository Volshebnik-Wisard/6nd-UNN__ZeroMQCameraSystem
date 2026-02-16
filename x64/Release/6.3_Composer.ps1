# Запуск Composer с логированием
$Host.UI.RawUI.WindowTitle = "6.3_Composer"
.\6.3_Composer.exe | Tee-Object -FilePath "Composer.txt"
pause