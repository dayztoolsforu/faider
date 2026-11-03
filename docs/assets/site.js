// Faider site: glide-in on scroll and the newest installer.
(function () {
  // Things glide in as they come into view.
  var items = document.querySelectorAll('.reveal');
  if ('IntersectionObserver' in window) {
    var seen = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (e.isIntersecting) { e.target.classList.add('shown'); seen.unobserve(e.target); }
      });
    }, { threshold: 0.12 });
    items.forEach(function (el) { seen.observe(el); });
  } else {
    items.forEach(function (el) { el.classList.add('shown'); });
  }

  // DOWNLOAD goes straight to the newest installer (pre-releases included).
  fetch('https://api.github.com/repos/dayztoolsforu/faider/releases?per_page=1')
    .then(function (r) { return r.ok ? r.json() : []; })
    .then(function (releases) {
      var rel = releases && releases[0];
      if (!rel) return;
      var exe = (rel.assets || []).filter(function (x) { return /Setup\.exe$/i.test(x.name); })[0];
      if (exe) document.querySelectorAll('[data-download]').forEach(function (a) { a.href = exe.browser_download_url; });
      document.querySelectorAll('[data-version]').forEach(function (s) {
        s.textContent = 'Version ' + rel.tag_name.replace(/^v/, '') + (rel.prerelease ? ' (pre-release)' : '') + ' · free and open source';
      });
    })
    .catch(function () {});
})();
