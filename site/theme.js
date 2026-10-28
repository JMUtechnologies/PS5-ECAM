// Runs before the page draws: the theme the reader picked (light or dark; none follows the system),
// and the "js" class the appearing sections wait for.
(function () {
  var root = document.documentElement;
  root.classList.add('js');
  try {
    var theme = localStorage.getItem('ps5-ecam-theme');
    if (theme === 'light' || theme === 'dark') root.setAttribute('data-theme', theme);
  } catch (e) {}
})();
