/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork, see docs/release-notes.md
 */
(() => {
  'use strict';

  function comparisonGalleries(content) {
    const guides = new Map(Array.from(document.querySelectorAll('.md-nav--primary a[href*="widgets/"]'),
      link => [link.textContent.trim(), link.href]));
    for (const table of content.querySelectorAll('table')) {
      const headers = Array.from(table.querySelectorAll('thead th'), th => th.textContent.trim());
      if (headers.slice(1, 5).join(',') !== 'Flat,Glow,Glass,Matte') continue;
      if (!table.querySelector('img[src*="img/gallery/"]')) continue;

      const comparisons = document.createElement('div');
      for (const row of table.querySelectorAll('tbody tr')) {
        if (!row.querySelector('img')) continue;
        const cells = row.cells;
        const section = document.createElement('section');
        section.className = 'gallery-comparison';
        const title = document.createElement('h3');
        title.textContent = cells[0].textContent.trim();
        if (guides.has(title.textContent)) {
          const guide = document.createElement('a');
          guide.href = guides.get(title.textContent);
          guide.textContent = title.textContent;
          title.replaceChildren(guide);
        }
        section.append(title);
        const styles = document.createElement('div');
        styles.className = 'gallery-styles';
        for (let column = 1; column <= 4; column++) {
          const style = document.createElement('div');
          style.className = 'gallery-style';
          const heading = document.createElement('h4');
          heading.textContent = headers[column];
          style.append(heading, ...Array.from(cells[column].childNodes));
          styles.append(style);
        }
        section.append(styles);
        if (cells[5]?.textContent.trim()) {
          const details = document.createElement('details');
          details.className = 'gallery-capabilities';
          const summary = document.createElement('summary');
          summary.textContent = `${title.textContent}: capabilities and limits`;
          const description = document.createElement('div');
          description.append(...Array.from(cells[5].childNodes));
          details.append(summary, description);
          section.append(details);
        }
        for (const img of styles.querySelectorAll('img')) {
          const sizePreview = () => {
            // One common pixel scale, with no enlargement of smaller widgets.
            img.style.width = `${img.naturalWidth * 0.8}px`;
            if (img.naturalWidth > 400) section.classList.add('gallery-comparison--wide');
          };
          if (img.complete && img.naturalWidth) sizePreview();
          else img.addEventListener('load', sizePreview, { once: true });
        }
        comparisons.append(section);
      }
      // Keep the theme's optional scroll wrapper out of the new card layout.
      const wrapper = table.closest('.md-typeset__scrollwrap');
      (wrapper || table).replaceWith(comparisons);
    }
  }

  function init() {
    const content = document.querySelector('.md-content__inner');
    if (!content) return;
    comparisonGalleries(content);
    const dialog = document.createElement('dialog');
    dialog.className = 'image-viewer';
    dialog.setAttribute('aria-label', 'Enlarged image');
    const toolbar = document.createElement('div');
    toolbar.className = 'image-viewer-toolbar';
    const original = document.createElement('a');
    original.textContent = 'Open original image';
    const close = document.createElement('button');
    close.type = 'button';
    close.className = 'image-viewer-close';
    close.textContent = 'Close';
    const image = document.createElement('img');
    image.className = 'image-viewer-image';
    function sizeImage(width) {
      // A lazy thumbnail may not have dimensions when clicked. Never retain a zero-width zoom after it loads.
      if (width > 0) image.style.width = `${Math.max(width, Math.min(800, width * 2))}px`;
      else image.style.removeProperty('width');
    }
    image.addEventListener('load', () => sizeImage(image.naturalWidth));
    const caption = document.createElement('p');
    caption.className = 'image-viewer-caption';
    caption.id = 'image-viewer-caption';
    dialog.setAttribute('aria-describedby', caption.id);
    toolbar.append(original, close);
    dialog.append(toolbar, image, caption);
    document.body.append(dialog);
    close.addEventListener('click', () => dialog.close());
    dialog.addEventListener('click', event => {
      if (event.target !== dialog) return;
      const rect = dialog.getBoundingClientRect();
      if (event.clientX < rect.left || event.clientX > rect.right ||
          event.clientY < rect.top || event.clientY > rect.bottom) dialog.close();
    });

    for (const img of content.querySelectorAll('img')) {
      let link = img.closest('a');
      if (link && link.href !== img.src) continue;
      if (!link) {
        link = document.createElement('a');
        link.href = img.src;
        img.replaceWith(link);
        link.append(img);
      }
      link.classList.add('image-zoom');
      link.setAttribute('aria-label', `Enlarge image: ${img.alt || 'preview'}`);
      link.addEventListener('click', event => {
        if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
        if (typeof dialog.showModal !== 'function') return;
        event.preventDefault();
        image.src = img.src;
        sizeImage(img.naturalWidth);
        image.alt = img.alt;
        caption.textContent = img.alt;
        original.href = img.src;
        dialog.showModal();
        close.focus();
      });
    }
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init, { once: true });
  else init();
})();
