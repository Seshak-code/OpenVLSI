import sys
import os

def parse_db(file_path):
    data = {}
    with open(file_path, 'r') as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            parts = line.split('|')
            if len(parts) < 4: continue
            
            name = parts[0]
            try:
                kcl = float(parts[3])
                iter_count = int(parts[2])
                
                if name not in data:
                    data[name] = {'kcl': [], 'iter': []}
                
                data[name]['kcl'].append(kcl)
                data[name]['iter'].append(iter_count)
            except ValueError:
                continue
    return data

def generate_html(data, output_dir):
    names = list(data.keys())
    if not names:
        print("No data found to plot.")
        return

    # Simple scaling for SVG bars
    kcls = [data[n]['kcl'][0] for n in names]
    iters = [data[n]['iter'][0] for n in names]
    
    max_iter = max(iters) if iters else 1
    
    html = """<!DOCTYPE html>
<html>
<head>
    <title>Regression Trend Analytics</title>
    <style>
        body { font-family: sans-serif; padding: 20px; background: #f0f0f0; }
        .chart-container { background: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        h2 { border-bottom: 2px solid #333; padding-bottom: 10px; }
        .bar-group { margin-bottom: 5px; }
        .bar-label { display: inline-block; width: 150px; font-size: 12px; overflow: hidden; text-overflow: ellipsis; vertical-align: middle; }
        .bar { display: inline-block; height: 16px; background: #2196F3; vertical-align: middle; border-radius: 3px; }
        .bar.warn { background: #FF9800; }
        .bar.err { background: #F44336; }
        .bar-val { font-size: 11px; color: #666; margin-left: 5px; }
    </style>
</head>
<body>
    <h1>Regression Analytics Snapshot</h1>
"""
    
    # KCL Chart (Log Scale simulated by listing values, bars normalized to threshold)
    html += '<div class="chart-container"><h2>KCL Residuals (Log Scale, Threshold 1pA)</h2>'
    for i, name in enumerate(names):
        val = kcls[i]
        # Log scale visualization is hard in pure width, so we verify against threshold
        # If val > 1e-12, red. Else green.
        width = 100
        color = "background:#4CAF50;" # Green
        if val > 1e-12: color = "background:#F44336;" # Red
        if val > 1e-9: width = 300
        elif val > 1e-12: width = 200
        else: width = 50
        
        html += f"""
        <div class="bar-group">
            <span class="bar-label" title="{name}">{name}</span>
            <div class="bar" style="width: {width}px; {color}"></div>
            <span class="bar-val">{val:.2e} A</span>
        </div>
        """
    html += '</div>'

    # Iterations Chart
    html += '<div class="chart-container"><h2>Newton Iterations (Warning > 50)</h2>'
    for i, name in enumerate(names):
        val = iters[i]
        pct = min(100, (val / 100) * 100) # Cap at 100 iterations for visual
        color = "#2196F3"
        if val > 50: color = "#FF9800"
        
        html += f"""
        <div class="bar-group">
            <span class="bar-label" title="{name}">{name}</span>
            <div class="bar" style="width: {pct}%; background: {color};"></div>
            <span class="bar-val">{val} iter</span>
        </div>
        """
    html += '</div></body></html>'

    out_path = os.path.join(output_dir, 'analytics_snapshot.html')
    with open(out_path, 'w') as f:
        f.write(html)
    print(f"Generated analytics report: {out_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 trend_analytics.py <path_to_regression_golden.db> [output_dir]")
        sys.exit(1)
        
    db_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    
    data = parse_db(db_path)
    generate_html(data, out_dir)
