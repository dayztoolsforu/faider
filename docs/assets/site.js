// Faider site: glide-in on scroll, the manual's section list, and the newest installer.
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

  // The manual's sections down the left, lit while in view.
  var toc = document.getElementById('toc');
  if (toc) {
    var heads = document.querySelectorAll('.prose h2');
    var links = [];
    heads.forEach(function (h) {
      var anchor = h.previousElementSibling && h.previousElementSibling.querySelector && h.previousElementSibling.querySelector('a[id]');
      var id = anchor ? anchor.id : h.id;
      if (!id) return;
      var li = document.createElement('li');
      var a = document.createElement('a');
      a.href = '#' + id;
      a.textContent = h.textContent;
      li.appendChild(a);
      toc.appendChild(li);
      links.push({ a: a, h: h });
    });
    if (links.length && 'IntersectionObserver' in window) {
      var watch = new IntersectionObserver(function (entries) {
        entries.forEach(function (e) {
          if (!e.isIntersecting) return;
          links.forEach(function (l) { l.a.classList.toggle('on', l.h === e.target); });
        });
      }, { rootMargin: '-80px 0px -70% 0px' });
      links.forEach(function (l) { watch.observe(l.h); });
    }
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
